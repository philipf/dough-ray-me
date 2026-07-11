#ifndef DOUGH_RAY_ME_SAFETY_H
#define DOUGH_RAY_ME_SAFETY_H

// Pure safety gate for dough-ray-me. Sits on top of the control decision
// (control.h) and enforces ADR-0001: safety overrides the control law. Like
// control.h it has no Arduino, LCD, or sensor dependencies, so it builds and
// runs on the host for testing.
//
// The gate can only ever turn the heater further OFF, never ON: it takes the
// heater command the control law wants (controlHeat) and passes it through
// unchanged during normal operation, or forces it OFF when a fault or the
// Safety Cutoff fires. It never fabricates an ON.
//
// Three things force the heater OFF, each raising a distinct Alarm for the LCD:
//   * Sensor Fault  -- the probe is disconnected or reading out of range, so
//                      the Box Air Temperature is unknown (sensorOk == false).
//                      This also covers boot: before the first valid reading the
//                      caller passes sensorOk == false, so the heater stays OFF.
//   * Safety Cutoff -- the Box Air Temperature reached the hard 35 C ceiling,
//                      independent of the Setpoint. This is latching with
//                      hysteresis: once tripped it stays tripped, re-arming only
//                      once the box cools back to 33 C.
//
// Carrying the latch through a pure function: the Safety Cutoff needs memory of
// whether it is currently tripped, exactly as decideHeat() needs currentHeat to
// hold its band. So the latch is threaded in and out explicitly as
// overTempLatched -- fed in from the previous call and returned updated, the way
// currentHeat is. It is kept separate from the display Alarm on purpose: a
// transient Sensor Fault while over-temp must not silently clear the latch (we
// cannot confirm the box cooled to 33 C without a valid reading), so while the
// sensor is faulted the latch is simply held.

enum SafetyAlarm {
  ALARM_NONE = 0,       // normal operation -- heater command passes through
  ALARM_SENSOR_FAULT,   // probe disconnected / out of range (also pre-first-reading)
  ALARM_OVER_TEMP,      // Box Air Temperature at/above the Safety Cutoff
};

struct SafetyDecision {
  bool        heatOn;           // gated heater command (never above controlHeat)
  SafetyAlarm alarm;            // what the LCD should show (ALARM_NONE == Home)
  bool        overTempLatched;  // Safety Cutoff latch, feed back in next call
};

// The hard over-temperature limits. Compile-time constants, never keypad-editable
// (ADR-0001): the cutoff is a safety limit, not a baker preference.
const float SAFETY_CUTOFF_C = 35.0;  // heater forced OFF at/above this
const float SAFETY_REARM_C  = 33.0;  // latch re-arms once cooled to/below this

inline SafetyDecision safetyGate(float tempC, bool sensorOk, bool controlHeat,
                                 bool overTempLatched) {
  SafetyDecision out;

  // Update the Safety Cutoff latch. We can only judge the temperature against
  // the 35/33 band when we trust the reading; on a Sensor Fault we hold the
  // latch as-is (cannot confirm the box has cooled to the re-arm point).
  if (sensorOk) {
    if (tempC >= SAFETY_CUTOFF_C)      overTempLatched = true;   // trip
    else if (tempC <= SAFETY_REARM_C)  overTempLatched = false;  // re-arm
    // between 33 C and 35 C: hold whatever the latch was doing (hysteresis).
  }
  out.overTempLatched = overTempLatched;

  // Apply the overrides. Sensor Fault takes precedence for the Alarm shown,
  // since an unknown temperature is the more fundamental problem.
  if (!sensorOk) {
    out.heatOn = false;
    out.alarm  = ALARM_SENSOR_FAULT;
  } else if (overTempLatched) {
    out.heatOn = false;
    out.alarm  = ALARM_OVER_TEMP;
  } else {
    out.heatOn = controlHeat;   // normal: pass the control decision through
    out.alarm  = ALARM_NONE;
  }
  return out;
}

#endif  // DOUGH_RAY_ME_SAFETY_H
