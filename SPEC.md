# dough-ray-me — Fermentation Temperature Controller

## Problem Statement

I bake sourdough in a cold New Zealand kitchen, where the dough won't reach the
right fermentation level in a workable time. Waiting longer for a slow ferment throws
my schedule around, and I can't plan the rest of my day around an unpredictable rise. I
need to hold the dough (and my starter) at a chosen, stable temperature so the ferment
is *predictable* and I can fit baking into my life.

## Solution

An Arduino Uno drives a closed-loop temperature controller for an insulated polystyrene
**Fermenting Box**. A single probe reads the **Box Air Temperature**; a solid-state relay
switches a light bulb to add heat; the box cools passively when the bulb is off. I dial a
**Setpoint** on the LCD Keypad Shield and the controller holds the box within a chosen
**Tolerance** of it, so bulk fermentation and starter upkeep run on a schedule I control.
It is a closed system: no networking, no cooling hardware, unplug-and-done.

## User Stories

1. As a baker, I want the box air held at a temperature I choose, so that my dough
   ferments predictably instead of stalling in a cold kitchen.
2. As a baker, I want to set a target temperature between 18 and 30 °C, so that I can pick
   a cool slow ferment or a warm fast one to suit my schedule.
3. As a baker, I want to adjust the Setpoint in 0.5 °C steps, so that I can fine-tune the
   ferment speed without a lot of fiddling.
4. As a baker, I want the controller to boot to a sensible 24 °C default, so that I can
   just plug it in and it does the right thing without configuration.
5. As a baker, I want my chosen Setpoint remembered across a power cut, so that a bump or
   blip mid-bake doesn't silently reset my ferment to the default.
6. As a baker, I want the box to hold within a Tolerance I can set, so that I can trade a
   tighter temperature for less frequent bulb cycling as I learn the box.
7. As a baker, I want to adjust the Tolerance from the keypad (±0.25 to ±2.0 °C), so that I
   can tune the box's behaviour without plugging into a laptop.
8. As a baker, I want my Tolerance remembered across a power cut, so that my tuning isn't
   lost when the power blips.
9. As a baker, I want the heater to only add heat (never cool), so that the build stays
   simple and I cool the box just by turning the bulb off.
10. As a baker, I want to see the current Box Air Temperature at a glance, so that I know
    what the dough is actually experiencing right now.
11. As a baker, I want the home screen to show temperature, Setpoint, heat state and
    Tolerance together, so that I understand the whole state as I walk past the box.
12. As a baker, I want to adjust the Setpoint straight from the home screen with Up/Down,
    so that my most common action takes the fewest button presses.
13. As a baker, I want dedicated Setpoint and Tolerance screens reached with Left/Right,
    so that I can edit either value deliberately.
14. As a baker, I want edits to take effect live with no confirm step, so that adjusting is
    immediate and I never wonder whether a change was saved.
15. As a baker, I want Up/Down to auto-repeat when held, so that ramping the Setpoint several
    degrees is a press-and-hold instead of many taps.
16. As a baker, I want a Select button that always returns me to the home screen, so that I
    have a reliable escape from any edit screen.
17. As a baker, I want a Stats screen showing the min and max Box Air Temperature since
    power-on, so that I can see the real swing the box settles into and judge my Tolerance.
18. As a baker, I want the Stats screen to show Heater Duty (percent of time the bulb has
    been on), so that I can tell whether the box is barely coping or has headroom.
19. As a baker, I want to reset the Stats by pressing the shield's RESET button, so that I
    can start a fresh observation without losing my saved Setpoint and Tolerance.
20. As a baker, I want the heater forced off if the probe disconnects or reads garbage, so
    that a bulb never heats a box whose temperature is unknown.
21. As a baker, I want a clear Alarm on the LCD when a fault forces the heater off, so that
    a cold or cut-out box is never mistaken for normal running.
22. As a baker, I want a hard 35 °C Safety Cutoff independent of the Setpoint, so that a bug,
    sensor drift or a wild setting can never let the box overheat.
23. As a baker, I want the heater to stay off until the first valid reading on boot, so that
    the controller never heats blindly at start-up.
24. As a baker, I want the buttons to feel responsive at all times, so that editing never
    feels laggy or drops my presses while the sensor is being read.
25. As a baker, I want optional serial logging over USB, so that I can capture hours of box
    behaviour on a laptop when I want to study it, without any of it leaving the box otherwise.
26. As a baker, I want to unplug the controller to stop everything, so that shutting down is
    as simple as pulling the plug.

## Implementation Decisions

- **Target platform.** Arduino Uno (`arduino:avr:uno`), breadboard wiring. Product firmware
  lives at `firmware/dough-ray-me/` as a graduated sketch, separate from the `poc/`
  experiments which remain as history. Libraries: `LiquidCrystal`, `OneWire`,
  `DallasTemperature`; EEPROM via the AVR core (no library entry).
- **Pin map** (carried over unchanged from the thermostat PoC): D2 DS18B20 data (4.7 kΩ
  pull-up); D3 relay IN (active-HIGH) → bulb; D4–D7 LCD data; D8/D9 LCD RS/E; D10 LCD
  backlight; D13 LED_BUILTIN mirrors the relay; A0 keypad analog ladder.
- **Controlled variable.** A single probe reads Box Air Temperature, treated as a proxy for
  both dough and starter; neither is measured directly.
- **Control law.** Hysteresis on the raw reading: heat ON below `Setpoint − Tolerance`, OFF
  above `Setpoint + Tolerance`, hold between. Tolerance is a ± half-band, so total swing is
  `2 × Tolerance`.
- **Setpoint.** Boots 24 °C; editable 18–30 °C in 0.5 °C steps.
- **Tolerance.** Default ±0.5 °C; editable ±0.25 to ±2.0 °C in 0.25 °C steps.
- **Persistence.** Setpoint and Tolerance persist to EEPROM, written only after a value has
  stopped changing for ~2 s (debounced) so holding to ramp causes one write per adjustment,
  not per step. Both are read back on boot.
- **Safety (ADR-0001).** Sensor fault → heater OFF + Alarm. Hard 35 °C Safety Cutoff,
  independent of the Setpoint, re-arming at 33 °C, as a compile-time constant not reachable
  from the menu. Heater OFF until the first valid reading.
- **Architecture (ADR-0002).** Non-blocking main loop. `millis()`-scheduled ~1 s sensor
  sampling read asynchronously (`setWaitForConversion(false)`); buttons scanned every few ms
  with edge detection and hold auto-repeat; LCD repainted only on change. No `delay()` in the
  loop.
- **UI model.** 16×2 LCD + 5 buttons. Four screens paged with Left/Right: Home (read-only
  status) · Setpoint · Tolerance · Stats (min/max temp + Heater Duty). Up/Down edit the
  current screen's value live; Up/Down also adjust the Setpoint from Home. Select returns to
  Home. The shield's physical RESET reboots the Arduino, clearing since-power-on Stats while
  Setpoint/Tolerance survive via EEPROM.
- **Observability.** Serial logging once per sample (temp / Setpoint / Tolerance / heat state
  / duty) for multi-hour tuning; USB cable only, no networking.

## Testing Decisions

- **What makes a good test here.** Test external behaviour — given a temperature, Setpoint and
  Tolerance, does the controller command heat on/off correctly; does the Safety Cutoff and
  sensor-fault fail-safe override the control law; does a sequence of button events move
  through the screen/edit state machine as specified. Not the wiring of specific pins.
- **The seam.** Factor the pure decision logic out of the Arduino I/O so it can be compiled and
  run on the host (native), with no `LiquidCrystal`/`OneWire`/hardware dependencies. Highest
  useful seams, ideally these three pure units:
  1. **Control decision** — `(tempC, setpoint, tolerance, currentHeat) → heatOn` including the
     hysteresis hold band.
  2. **Safety gate** — `(tempC, sensorOk, currentHeat) → (heatOn, alarmState)` enforcing the
     35 °C/33 °C cutoff and sensor-fault fail-safe, applied on top of the control decision.
  3. **UI state machine** — `(state, buttonEvent) → state` for screen navigation and live
     value editing with clamping to the Setpoint/Tolerance ranges and steps.
  The `.ino` becomes a thin shell that reads hardware, calls these pure units, and drives the
  LCD/relay — deliberately kept dumb so the untested surface is only glue.
- **Modules tested.** The three pure units above. Hardware glue (pin reads/writes, LCD paint,
  EEPROM read/write, analog-ladder button decode) is verified manually on the breadboard.
- **Prior art.** None in-repo yet — the `poc/` sketches are hardware smoke tests. This spec
  introduces the first host-buildable logic; keep the harness minimal and dependency-free in
  the spirit of a hobby build.

## Out of Scope

- **Cooling** — heat is only added by the bulb; the box cools passively when it's off.
- **Networking / connectivity (ADR-0003)** — closed-loop system; nothing leaves the box except
  optional serial over a physically attached USB cable.
- **Fermentation timer or "ready" alert (ADR-0003)** — the box makes timing *predictable*; the
  baker uses an ordinary kitchen timer for the countdown.
- **Stuck-relay detection** — cannot be detected without extra hardware (e.g. a current sensor);
  accepted risk for a breadboard hobby build.
- **Measuring dough or starter temperature directly** — the Box Air Temperature is the single
  controlled variable and proxy for both.
- **Box construction, PCB design, and physical wiring** — hardware is hand-wired on a breadboard
  by the baker; this project is the Arduino logic only.

## Further Notes

- Fermentation temperature guidance was verified: 22–26 °C is workable, 23–24 °C the common
  sweet spot; the 24 °C default and 18–30 °C range sit sensibly around that.
- Domain vocabulary is recorded in `CONTEXT.md`; the three cross-cutting decisions are in
  `docs/adr/0001`–`0003`. Ticket titles and descriptions should use that glossary.
