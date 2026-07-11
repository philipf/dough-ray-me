# Fail-safe heating with an independent over-temp cutoff

dough-ray-me heats a light bulb inside a sealed, insulated polystyrene box, so any
software fault must fail toward OFF rather than leave the bulb heating an unknown box.
We therefore (1) force the heater OFF on any sensor fault (disconnected or out-of-range
reading), and (2) enforce a hard 35 °C Safety Cutoff that is independent of the Setpoint
control law — if the Box Air Temperature exceeds 35 °C the heater goes OFF regardless of
what the Setpoint/Tolerance logic wants, re-arming only once it falls back to 33 °C. Both
conditions raise a visible LCD Alarm so a cut-out box is never mistaken for normal running.

The cutoff is a compile-time constant, not a keypad setting: it is a safety limit, not a
baker preference, and must not be reachable through the menu. 35 °C sits comfortably above
the hottest useful Setpoint (30 °C) yet well below anything that would harm the culture.

We cannot detect a physically stuck relay without extra hardware (e.g. a current sensor);
that is accepted as out of scope for a breadboard hobby build. The software guards above
are the mitigations we can implement.
