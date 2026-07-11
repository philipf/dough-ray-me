// Host tests for the pure Stats accumulators. Dependency-free: a tiny CHECK
// macro, compiled and run natively (see run.sh). Expected values come from the
// Stats definitions in CONTEXT.md / SPEC.md (min/max Box Air Temperature and
// Heater Duty since power-on), not from re-deriving the implementation. The LCD
// rendering and the millis() timing that feeds elapsed intervals are impure glue
// in the .ino; only the pure accumulation is exercised here.
#include <cstdio>
#include "../stats.h"

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

static bool near(float a, float b) {
  float d = a - b;
  return (d < 0 ? -d : d) < 1e-4f;
}

int main() {
  // --- Fresh state: nothing seen, no duty -----------------------------------
  StatsState s = statsInitial();
  CHECK(s.seenTemp == false);
  CHECK(statsDutyPercent(s) == 0);          // 0 % before any time has elapsed

  // --- Min/max: first reading seeds both ends -------------------------------
  s = statsObserveTemp(s, 24.0f);
  CHECK(s.seenTemp == true);
  CHECK(near(s.minTempC, 24.0f));
  CHECK(near(s.maxTempC, 24.0f));

  // A cooler reading lowers only the min; a warmer one raises only the max.
  s = statsObserveTemp(s, 23.2f);
  CHECK(near(s.minTempC, 23.2f));
  CHECK(near(s.maxTempC, 24.0f));
  s = statsObserveTemp(s, 24.8f);
  CHECK(near(s.minTempC, 23.2f));
  CHECK(near(s.maxTempC, 24.8f));

  // A reading inside the current swing widens neither end.
  s = statsObserveTemp(s, 24.0f);
  CHECK(near(s.minTempC, 23.2f));
  CHECK(near(s.maxTempC, 24.8f));

  // Exact-edge readings equal to the current min/max are no-ops (strict <, >).
  s = statsObserveTemp(s, 23.2f);
  s = statsObserveTemp(s, 24.8f);
  CHECK(near(s.minTempC, 23.2f));
  CHECK(near(s.maxTempC, 24.8f));

  // --- Heater Duty: all OFF, all ON, and a half-and-half split ---------------
  s = statsInitial();
  s = statsAccrue(s, false, 1000);          // 1 s with the heater OFF
  CHECK(statsDutyPercent(s) == 0);          // 0 % ON
  s = statsAccrue(s, true, 1000);           // 1 s with the heater ON
  CHECK(statsDutyPercent(s) == 50);         // 1 s ON of 2 s -> 50 %

  s = statsInitial();
  s = statsAccrue(s, true, 4000);           // 4 s ON, 0 s OFF
  CHECK(statsDutyPercent(s) == 100);        // fully ON -> 100 %

  // --- Duty counts the real relay state, incl. safety-forced OFF -------------
  // A stretch the safety gate forces OFF is accrued with heatOn == false, so it
  // pulls the duty down exactly like any other OFF time.
  s = statsInitial();
  s = statsAccrue(s, true, 1000);           // 1 s heater actually ON
  s = statsAccrue(s, false, 3000);          // 3 s forced OFF (e.g. Safety Cutoff)
  CHECK(statsDutyPercent(s) == 25);         // 1 s of 4 s -> 25 %

  // --- Rounding to the nearest whole percent ---------------------------------
  s = statsInitial();
  s = statsAccrue(s, true, 1000);
  s = statsAccrue(s, false, 2000);          // 1 s of 3 s = 33.33.. % -> 33 %
  CHECK(statsDutyPercent(s) == 33);
  s = statsInitial();
  s = statsAccrue(s, true, 2000);
  s = statsAccrue(s, false, 1000);          // 2 s of 3 s = 66.66.. % -> 67 %
  CHECK(statsDutyPercent(s) == 67);

  // --- Many small increments accumulate the same as one big one --------------
  s = statsInitial();
  for (int i = 0; i < 500; ++i) s = statsAccrue(s, true, 5);   // 2500 ms ON
  for (int i = 0; i < 500; ++i) s = statsAccrue(s, false, 5);  // 2500 ms OFF
  CHECK(statsDutyPercent(s) == 50);

  // --- A multi-hour bake must not overflow the duty math (64-bit widen) ------
  // ~12 h all ON: on 32-bit millis, heaterOnMs*100 would overflow without the
  // widen; the result must still read a clean 100 %.
  s = statsInitial();
  s = statsAccrue(s, true, 43200000UL);     // 12 h in ms
  CHECK(statsDutyPercent(s) == 100);

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
