#ifndef DOUGH_RAY_ME_STATS_H
#define DOUGH_RAY_ME_STATS_H

#include <stdint.h>

// Pure Stats accumulators for dough-ray-me: the read-only numbers shown on the
// Stats screen so the baker can characterise the box over a bake. Like control.h
// and safety.h it has no Arduino, LCD, sensor, or millis() dependency, so it
// builds and runs on the host for testing. All state lives in a StatsState that
// the caller threads in and out (value semantics) -- the .ino owns the single
// instance and, on the shield's physical RESET, the Arduino reboots and this
// starts fresh at statsInitial() while the Setpoint/Tolerance survive in EEPROM.
//
// Two things are tracked since power-on:
//   * Min / max Box Air Temperature -- the real swing the box settles into, so
//     the baker can judge whether the Tolerance is doing what they expect. Only
//     valid readings are observed; seenTemp guards the pair until the first one.
//   * Heater Duty -- the fraction of time the bulb has actually been ON. Elapsed
//     time is threaded in (not read from a clock here) and accrued against the
//     real relay state, so time the safety gate forces the heater OFF correctly
//     counts as OFF, not as what the control law wanted.

struct StatsState {
  bool          seenTemp;    // false until the first valid reading is observed
  float         minTempC;    // min Box Air Temperature since power-on
  float         maxTempC;    // max Box Air Temperature since power-on
  unsigned long heaterOnMs;  // accumulated ms the heater relay has been ON
  unsigned long elapsedMs;   // accumulated ms observed since power-on
};

inline StatsState statsInitial() {
  StatsState s;
  s.seenTemp   = false;
  s.minTempC   = 0.0f;   // meaningless until seenTemp; the first reading seeds both
  s.maxTempC   = 0.0f;
  s.heaterOnMs = 0;
  s.elapsedMs  = 0;
  return s;
}

// Fold a valid Box Air Temperature reading into the min/max pair. The first
// reading seeds both ends; later readings only widen the swing.
inline StatsState statsObserveTemp(StatsState s, float tempC) {
  if (!s.seenTemp) {
    s.seenTemp = true;
    s.minTempC = tempC;
    s.maxTempC = tempC;
  } else {
    if (tempC < s.minTempC) s.minTempC = tempC;
    if (tempC > s.maxTempC) s.maxTempC = tempC;
  }
  return s;
}

// Accrue Heater Duty from an elapsed interval. heatOn is the actual relay state
// over that interval (post safety gate), so a Safety Cutoff or Sensor Fault that
// forces the heater OFF is counted as OFF here.
inline StatsState statsAccrue(StatsState s, bool heatOn, unsigned long deltaMs) {
  s.elapsedMs += deltaMs;
  if (heatOn) s.heaterOnMs += deltaMs;
  return s;
}

// Heater Duty as a whole percent (0-100), rounded. Zero before any time has
// elapsed. The multiply is widened to 64-bit so a multi-hour bake can't overflow
// the 32-bit millis() math on the Uno.
inline int statsDutyPercent(StatsState s) {
  if (s.elapsedMs == 0) return 0;
  uint64_t pct = ((uint64_t)s.heaterOnMs * 100 + s.elapsedMs / 2) / s.elapsedMs;
  return (int)pct;
}

#endif  // DOUGH_RAY_ME_STATS_H
