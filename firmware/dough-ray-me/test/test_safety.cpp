// Host tests for the pure safety gate. Dependency-free: a tiny CHECK macro,
// compiled and run natively (see run.sh). Expected values come from ADR-0001 and
// the Safety Cutoff / Alarm rules in CONTEXT.md / SPEC.md, not from re-deriving
// the implementation.
#include <cstdio>
#include "../safety.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    ++checks;                                                             \
    if (!(cond)) {                                                        \
      ++failures;                                                         \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);         \
    }                                                                     \
  } while (0)

int main() {
  // Safety Cutoff 35 C, re-arm 33 C (compile-time constants, ADR-0001).

  // --- Normal operation: the gate passes the control decision straight through.
  {
    SafetyDecision d = safetyGate(24.0f, true, true, false);
    CHECK(d.heatOn == true);              // control wanted ON, sensor ok, cool box
    CHECK(d.alarm == ALARM_NONE);         // no Alarm -> normal Home
    CHECK(d.overTempLatched == false);
  }
  {
    SafetyDecision d = safetyGate(24.0f, true, false, false);
    CHECK(d.heatOn == false);             // control wanted OFF -> stays OFF
    CHECK(d.alarm == ALARM_NONE);
  }

  // --- The gate can only ever turn heat further OFF, never ON.
  // Even at a freezing, well-below-Setpoint temperature, a control decision of
  // OFF is never overridden to ON by the safety gate.
  {
    SafetyDecision d = safetyGate(5.0f, true, false, false);
    CHECK(d.heatOn == false);
    CHECK(d.alarm == ALARM_NONE);
  }

  // --- Sensor Fault forces OFF + Alarm, whatever the control law wanted.
  {
    SafetyDecision d = safetyGate(24.0f, false, true, false);
    CHECK(d.heatOn == false);
    CHECK(d.alarm == ALARM_SENSOR_FAULT);
  }

  // --- No heat before the first valid reading (boot). The caller signals "no
  // trustworthy reading yet" with sensorOk == false, so the heater stays OFF.
  {
    SafetyDecision d = safetyGate(0.0f, false, true, false);
    CHECK(d.heatOn == false);
    CHECK(d.alarm == ALARM_SENSOR_FAULT);
  }

  // --- Safety Cutoff: above 35 C forces OFF + Alarm and latches.
  {
    SafetyDecision d = safetyGate(35.5f, true, true, false);
    CHECK(d.heatOn == false);
    CHECK(d.alarm == ALARM_OVER_TEMP);
    CHECK(d.overTempLatched == true);
  }
  // Exactly at the 35 C ceiling trips too (at/above).
  {
    SafetyDecision d = safetyGate(35.0f, true, true, false);
    CHECK(d.heatOn == false);
    CHECK(d.alarm == ALARM_OVER_TEMP);
    CHECK(d.overTempLatched == true);
  }

  // --- Hysteresis: heat stays OFF between the 35 C trip and the 33 C re-arm.
  // Once latched, a temperature in the 33..35 band keeps the heater OFF and the
  // latch set, even though the control law (heat ON at this cool-ish temp) wants ON.
  {
    SafetyDecision d = safetyGate(34.0f, true, true, /*latched=*/true);
    CHECK(d.heatOn == false);
    CHECK(d.alarm == ALARM_OVER_TEMP);
    CHECK(d.overTempLatched == true);
  }
  // Still latched right down to just above the re-arm point.
  {
    SafetyDecision d = safetyGate(33.1f, true, true, true);
    CHECK(d.heatOn == false);
    CHECK(d.alarm == ALARM_OVER_TEMP);
    CHECK(d.overTempLatched == true);
  }

  // --- Re-arm: at/below 33 C the latch clears and normal operation resumes.
  {
    SafetyDecision d = safetyGate(33.0f, true, true, true);
    CHECK(d.heatOn == true);              // control decision passes through again
    CHECK(d.alarm == ALARM_NONE);
    CHECK(d.overTempLatched == false);
  }
  {
    SafetyDecision d = safetyGate(32.5f, true, false, true);
    CHECK(d.heatOn == false);             // re-armed; control law is back in charge
    CHECK(d.alarm == ALARM_NONE);
    CHECK(d.overTempLatched == false);
  }

  // --- A Sensor Fault while over-temp must not silently clear the latch. We
  // cannot confirm the box cooled to 33 C without a valid reading, so the latch
  // is held; the Alarm shows the Sensor Fault (the more fundamental problem).
  {
    SafetyDecision d = safetyGate(99.0f, false, true, /*latched=*/true);
    CHECK(d.heatOn == false);
    CHECK(d.alarm == ALARM_SENSOR_FAULT);
    CHECK(d.overTempLatched == true);     // still latched across the fault
  }

  // --- A full trip -> hold -> re-arm sequence, threading the latch call to call.
  {
    SafetyDecision a = safetyGate(36.0f, true, true, false);   // trip
    CHECK(a.heatOn == false && a.overTempLatched == true);
    SafetyDecision b = safetyGate(34.0f, true, true, a.overTempLatched);  // hold
    CHECK(b.heatOn == false && b.overTempLatched == true);
    SafetyDecision c = safetyGate(33.0f, true, true, b.overTempLatched);  // re-arm
    CHECK(c.heatOn == true && c.overTempLatched == false && c.alarm == ALARM_NONE);
  }

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
