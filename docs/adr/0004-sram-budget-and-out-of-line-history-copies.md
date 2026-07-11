# SRAM budget: keep large value-semantics copies out of line

The pure logic units (`control.h`, `safety.h`, `ui.h`, `stats.h`, `history.h`) all use value
semantics — the caller threads the state in and out (`s = historyAccrue(s, ...)`) — so they
build and run on the host for testing with no Arduino dependency. This decision keeps that
design but records a hard constraint it runs into on the ATmega328P, and how `history.h`
resolves it.

The Uno has **2 KB of SRAM**. Globals already use ~0.9 KB (the LCD/OneWire/Dallas objects,
the serial buffers, the `HistoryState`, and the string literals). The Arduino build inlines
the whole sketch into a single `main()`, and gcc gives **each** inlined by-value temporary its
own stack slot rather than reusing one. `HistoryState` is 228 bytes at 16 columns, so left as
plain `inline` the `historyAccrue` / `historyObserveTemp` / `historyRender` copies *sum* into
`main()`'s frame (~1.2 KB). Globals + that frame overflow SRAM: the stack lands on top of the
globals the instant `main()` is entered, memory corrupts, and the board resets in a loop —
before it prints its first serial line. It reads on the bench as a display stuck on the boot
splash, "flashing" once per reset, with a silent serial log.

We chose to mark those three functions `noinline` on AVR (a `HISTORY_NOINLINE` macro that is a
no-op on the host) and to pass the render input by `const&`. Each 228-byte copy then lives in a
short-lived frame that is freed on return, so the peak stack stays bounded while the value-
semantics API — and every host test — is unchanged. The alternative, rewriting the units to
mutate through a reference, would have been faster on-chip but would have split the codebase's
one consistent "thread the state in and out" pattern for a single unit, and changed its tests.

Consequences:

- The `noinline` markers in `history.h` are load-bearing, not stylistic. Do not "simplify" them
  back to plain `inline` — verify the sketch's `main()` frame first (`avr-objdump -d`, look at
  `main`'s `subi r28` / `sbci r29` reservation; it should be small, not ~1.2 KB).
- Growing a value-semantics unit's state struct (more `HISTORY_COLUMNS`, a wider `StatsState`,
  a new large unit) re-enters this budget. Check globals + worst-case `main()` frame against
  2 KB before assuming host-green means it runs on the board.
- Host tests and a clean compile do not exercise this failure — only the real chip does. A
  boot that streams the once-per-sample serial line is the cheap on-hardware smoke test.
