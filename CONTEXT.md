# dough-ray-me

An Arduino-based closed-loop temperature controller that holds a polystyrene box
at a chosen fermentation temperature, so sourdough can bulk-ferment and a starter
can stay active on schedule in a cold New Zealand kitchen. Closed system — no
network, no cooling; heat is added by a light bulb and removed only by letting the
box cool passively.

## Language

**Fermenting Box**:
The insulated polystyrene enclosure whose air temperature is regulated. Holds the
dough and the starter together during bulk fermentation. Not used for baking.
_Avoid_: proofing box, incubator, chamber

**Box Air Temperature**:
The temperature of the air inside the Fermenting Box, measured by the single probe.
This is the one controlled variable; it is treated as a proxy for both dough and
starter temperature, neither of which is measured directly.
_Avoid_: dough temperature, ambient temperature

**Bulk Fermentation**:
The stage this box exists to support — the first rise of the dough after mixing,
before shaping. Baking happens elsewhere and is out of scope.
_Avoid_: proofing, proving, rising

**Starter**:
The live sourdough culture kept in the box to stay active between bakes. Shares the
box (and therefore the Box Air Temperature) with the dough.
_Avoid_: levain, culture, mother

**Setpoint**:
The target Box Air Temperature the controller tries to hold. Chosen by the baker via
the keypad. Boots to 24 °C, adjustable 18–30 °C in 0.5 °C steps.
_Avoid_: target temperature, set temp, goal

**Tolerance**:
How far the Box Air Temperature is allowed to drift on _either side_ of the Setpoint
before the heater reacts — a ± half-band. Heat turns ON below `Setpoint − Tolerance`
and OFF above `Setpoint + Tolerance`, holding in between so the bulb doesn't chatter.
So the total peak-to-peak swing is `2 × Tolerance`. Keypad-editable and persisted like
the Setpoint. Default ±0.5 °C, adjustable ±0.25 to ±2.0 °C in 0.25 °C steps.
_Avoid_: band, hysteresis, deadband, tolerance range, swing, window

**Safety Cutoff**:
A fixed 35 °C ceiling, independent of the Setpoint, at which the heater is forced OFF
no matter what the control law wants. Re-arms once the box cools to 33 °C. Not
keypad-editable — it is a safety limit, not a preference.
_Avoid_: max temp, limit, overheat

**Alarm**:
The LCD state shown when a Safety Cutoff or Sensor Fault has forced the heater OFF,
so a cold or cut-out box is never silent or mistaken for normal operation.
_Avoid_: warning, error, fault (as UI label)

**Heater Duty**:
The fraction of time the bulb has been ON — a coarse read on how hard the box is
working to hold the Setpoint. Shown two ways: on the Stats screen as a single
since-power-on figure alongside the min/max Box Air Temperature, and on the Graph
screen sliced per time window, to help the baker characterise the box over a bake.
_Avoid_: duty cycle, load, on-time

**Graph screen**:
The read-only screen showing the last ~80 minutes of the Fermenting Box's behaviour
as a compact history: per-window Heater Duty above, and Box Air Temperature deviation
from the Setpoint below. Since-power-on and cleared by RESET, like the Stats screen.
_Avoid_: chart, trend, plot, log, history screen
