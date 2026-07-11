# Coding standards — dough-ray-me

How firmware in this repo is written. These are the conventions the existing code
(`firmware/dough-ray-me/`) already follows; a change should look like it belongs
next to `control.h`, `safety.h`, and `ui.h`. They exist to keep the safety-critical
logic testable on a laptop and the one interaction the baker performs most — live
keypad editing — always responsive.

This file is a **review input**: `/code-review` reads it as the repo's documented
standard. Keep it honest — describe what the code does, not an aspiration. Where a
rule here and an ADR disagree, the ADR wins and this file should be corrected.

## 1. Pure logic lives in dependency-free headers

The control-critical decisions are **pure units** in header files that include no
Arduino, LCD, sensor, or `millis()` dependency, so they compile and run on the host:

- `control.h` — `decideHeat(...)`, the hysteresis control law.
- `safety.h` — `safetyGate(...)`, the fail-safe applied on top of the control law.
- `ui.h` — `uiStep(state, button)`, the UI state machine.

Rules for a pure unit:

- **No I/O, no time, no globals.** A pure unit is a function of its arguments only.
  It reads no pins, calls no `analogRead`/`millis`/`Serial`, and mutates no module
  state. State that must persist across calls (the heater state, the over-temp
  latch, the UI screen/values) is **threaded in as a parameter and returned out** —
  see `decideHeat(..., bool currentHeat)` and `safetyGate(..., bool overTempLatched)`.
- **Value semantics.** Take inputs by value, return the new value/struct. `uiStep`
  takes a `UiState` and returns the next `UiState`; it does not mutate in place.
- **Header-guarded and `inline`.** Match the existing `#ifndef DOUGH_RAY_ME_*_H`
  guard and `inline` free functions so the header can be included by both the
  sketch and a host test without ODR clashes.
- **Named constants, not magic numbers.** Ranges, steps, and safety limits are
  named `const` in the header that owns them (e.g. `SAFETY_CUTOFF_C`,
  `SAFETY_REARM_C`, the Setpoint/Tolerance min/max/step). Safety limits are
  compile-time constants and **must not be reachable from the menu** (ADR-0001).

## 2. The `.ino` is a thin hardware shell

`dough-ray-me.ino` is the only file allowed to touch hardware and time. Its job is
to move data between the pins and the pure units:

- Read the sensor / keypad → build plain inputs → call the pure unit → apply the
  result to the relay and LCD. Keep decision logic *out* of the sketch; if you find
  yourself writing an `if` about temperature or clamping a value in the `.ino`, it
  probably belongs in a pure unit with a host test.
- Impure concerns that legitimately live in the sketch: pin decode (the A0 analog
  ladder thresholds), edge detection, auto-repeat **timing**, async sensor
  scheduling, and LCD rendering. The pure UI machine only ever sees discrete button
  *events*; the timing that generates them stays in the sketch.

## 3. Non-blocking loop — never `delay()` (ADR-0002)

`loop()` must never sleep. Schedule everything with `millis()` timers:

- Temperature sampling (~1 s) and the async DS18B20 conversion
  (`setWaitForConversion(false)`, ~750 ms) must not block.
- Scan the keypad every few milliseconds so a press is never swallowed.
- Do `millis()` comparisons wrap-safely: compare with subtraction
  (`(now - since) >= interval`) or a signed cast (`(long)(now - deadline) >= 0`),
  never `now >= since + interval`.
- No busy-wait loops, no `delay()`, anywhere in the running path.

## 4. Fail toward OFF (ADR-0001)

Heating an unknown or overheating sealed box is the hazard. Any new code on the
control path must fail toward the heater OFF:

- A bad or missing reading (disconnected, or outside the plausible sensor window)
  forces the heater OFF, never ON.
- The safety gate may only ever pass the control decision through or force it
  **further OFF** — it must never turn heat ON that the control law didn't ask for.
- Any force-OFF caused by a fault or the Safety Cutoff raises a visible **Alarm** on
  the LCD, distinct from the normal Home screen, so a cut-out box is never mistaken
  for normal running.

## 5. Use the ubiquitous language (`CONTEXT.md`)

Names in code, comments, and commit messages use the domain terms from `CONTEXT.md`
and avoid its listed synonyms. It is *Box Air Temperature*, *Setpoint*, *Tolerance*
(a ± half-band, **not** "hysteresis"/"deadband"), *Safety Cutoff*, *Alarm*,
*Fermenting Box*, *Heater Duty*. Don't introduce a new word for a concept that
already has one.

## 6. Host tests are mandatory for pure units (`test/`)

Every pure unit ships with a host test compiled and run by `test/run.sh`. A change
to a pure unit that doesn't update its test is incomplete.

- **Style:** dependency-free, the tiny `CHECK` macro from `test_control.cpp`, one
  `main()`, printing `N checks, M failures` and returning non-zero on failure. No
  test framework.
- **Derive expected values from the spec**, not by re-running the implementation in
  your head. Comments should cite the intended behaviour (the control law, the
  clamp range), so a wrong implementation fails the test rather than the test
  memorising the bug.
- **Cover the boundaries.** Test both edges of every band/range (e.g. the exact
  `Setpoint ± Tolerance` edges; clamping at *both* ends of Setpoint and Tolerance,
  including no-overshoot when already at a limit), the hold/hysteresis region, and
  the safety transitions (fault → OFF, > 35 °C → OFF, hold 33–35, re-arm at ≤ 33,
  boot before first reading).
- **Guard against over-fitting** to one parameter set: include at least one case
  with a different Setpoint/Tolerance, as `test_control.cpp` does.
- Add the new test's compile+run lines to `test/run.sh`; all suites must report
  `0 failures`. The Arduino sketch itself is not host-compilable — the pure units'
  host tests are the verification surface.

## 7. LCD: repaint only on change

Rendering follows the sentinel pattern in the sketch: track the last-shown value
(`shownTempDeci`, `shownScreen`, `shownAlarm`, …) and repaint a field only when it
changed, to avoid flicker on the non-blocking loop. Use the fixed-point
`toDeci`/`toCenti` helpers for cheap change detection rather than comparing floats.
An Alarm repaints the whole screen and overrides whatever screen is active.

## 8. Comments explain *why*, and stay honest

Match the existing comment density: a short block at the top of each unit stating
what it is and the invariant it holds, and inline comments for non-obvious *reasons*
(why the latch is threaded separately, why the sensor window sits below the DS18B20
85 °C glitch value). Don't narrate what the code plainly says. When a file notes
work deferred to a later ticket, keep that note accurate as tickets land.

## 9. Style mechanics

- 2-space indent, braces on the same line, matching the existing files.
- `const` for tuning/pin/timing values; `enum` for closed sets of states
  (`SafetyAlarm`, `UiScreen`, `UiButton`); a small `struct` when a few fields travel
  together (`SafetyDecision`, `UiState`) rather than out-parameters or parallel
  variables.
- Prefer a named type over a bare primitive when the value is a domain concept.
- Keep each file changing for one reason: control law in `control.h`, fail-safe in
  `safety.h`, UI in `ui.h`. Don't spread one concern across several files.
