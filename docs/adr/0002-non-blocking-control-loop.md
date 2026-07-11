# Non-blocking control loop

The firmware runs a non-blocking main loop rather than the `delay()`-based blocking loop
used in the thermostat PoC. The loop never sleeps: `millis()` timers schedule the ~1 s
temperature sampling, the DS18B20 is read asynchronously (`setWaitForConversion(false)`,
so its ~750 ms conversion doesn't block), buttons are scanned every few milliseconds, and
the LCD repaints only on change.

We chose this because live keypad editing (adjusting Setpoint and Tolerance by feel) only
feels responsive if button presses are never swallowed by a `delay()` or a blocking sensor
read. A blocking loop is simpler to write but makes the buttons periodically dead, which
undermines the one interaction the baker performs most.

Consequence: the sketch is structured around a small set of `millis()`-scheduled tasks and
an edge-detecting button reader instead of a single top-to-bottom pass. Do not reintroduce
`delay()` in the loop — it will bring back the laggy buttons this decision exists to avoid.
