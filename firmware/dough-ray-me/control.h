#ifndef DOUGH_RAY_ME_CONTROL_H
#define DOUGH_RAY_ME_CONTROL_H

// Pure hysteresis control decision for dough-ray-me. No Arduino, LCD, or sensor
// dependencies, so it builds and runs on the host for testing.
//
// Tolerance is a ± half-band around the Setpoint (see CONTEXT.md):
//   heat ON  at or below  Setpoint - Tolerance
//   heat OFF at or above  Setpoint + Tolerance
//   hold the current state in between, so the bulb doesn't chatter.
inline bool decideHeat(float tempC, float setpointC, float toleranceC, bool currentHeat) {
  if (tempC <= setpointC - toleranceC) return true;   // cold: turn heat ON
  if (tempC >= setpointC + toleranceC) return false;  // warm: turn heat OFF
  return currentHeat;                                 // in band: hold
}

#endif  // DOUGH_RAY_ME_CONTROL_H
