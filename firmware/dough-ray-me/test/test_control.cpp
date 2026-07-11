// Host tests for the pure control decision. Dependency-free: a tiny CHECK macro,
// compiled and run natively (see run.sh). Expected values come from the control
// law in CONTEXT.md / SPEC.md, not from re-deriving the implementation.
#include <cstdio>
#include "../control.h"

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
  // Setpoint 24.0, Tolerance +/-0.5  ->  ON at/below 23.5, OFF at/above 24.5.

  // Below the lower edge: heat ON regardless of the current state.
  CHECK(decideHeat(23.0f, 24.0f, 0.5f, false) == true);
  CHECK(decideHeat(23.0f, 24.0f, 0.5f, true) == true);

  // Above the upper edge: heat OFF regardless of the current state.
  CHECK(decideHeat(25.0f, 24.0f, 0.5f, true) == false);
  CHECK(decideHeat(25.0f, 24.0f, 0.5f, false) == false);

  // Inside the band: hold whatever the heater was doing (no chatter).
  CHECK(decideHeat(24.0f, 24.0f, 0.5f, true) == true);
  CHECK(decideHeat(24.0f, 24.0f, 0.5f, false) == false);
  CHECK(decideHeat(23.8f, 24.0f, 0.5f, true) == true);
  CHECK(decideHeat(24.2f, 24.0f, 0.5f, false) == false);

  // Exact band edges: lower edge turns ON, upper edge turns OFF.
  CHECK(decideHeat(23.5f, 24.0f, 0.5f, false) == true);   // Setpoint - Tolerance
  CHECK(decideHeat(24.5f, 24.0f, 0.5f, true) == false);   // Setpoint + Tolerance

  // A different Setpoint/Tolerance, to guard against over-fitting to 24/0.5.
  // Setpoint 20.0, Tolerance +/-1.0  ->  ON at/below 19.0, OFF at/above 21.0.
  CHECK(decideHeat(18.5f, 20.0f, 1.0f, false) == true);
  CHECK(decideHeat(21.5f, 20.0f, 1.0f, true) == false);
  CHECK(decideHeat(20.0f, 20.0f, 1.0f, true) == true);    // held in band

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
