// Host tests for the pure UI state machine. Dependency-free: a tiny CHECK macro,
// compiled and run natively (see run.sh). Expected screens, ranges and steps come
// from CONTEXT.md / SPEC.md, not from re-deriving the implementation. Only the
// pure (state, buttonEvent) -> state transition is exercised here; the keypad
// analog-ladder decode and auto-repeat timing are impure glue in the .ino.
#include <cstdio>
#include "../dough-ray-me/ui.h"

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

// Setpoint/Tolerance land on exact 0.5 / 0.25 grid values, all representable in
// binary float, so equality comparison is safe here.
static bool near(float a, float b) {
  float d = a - b;
  return (d < 0 ? -d : d) < 1e-4f;
}

int main() {
  // --- Navigation: Left/Right cycle the four screens and wrap ----------------
  UiState s = uiInitial();
  CHECK(s.screen == UI_HOME);
  CHECK(near(s.setpointC, 24.0f));      // boots to 24 C default
  CHECK(near(s.toleranceC, 0.5f));      // boots to +/-0.5 C default

  s = uiStep(s, UI_BTN_RIGHT);  CHECK(s.screen == UI_SETPOINT);
  s = uiStep(s, UI_BTN_RIGHT);  CHECK(s.screen == UI_TOLERANCE);
  s = uiStep(s, UI_BTN_RIGHT);  CHECK(s.screen == UI_STATS);
  s = uiStep(s, UI_BTN_RIGHT);  CHECK(s.screen == UI_HOME);   // wraps forward

  s = uiStep(s, UI_BTN_LEFT);   CHECK(s.screen == UI_STATS);  // wraps backward
  s = uiStep(s, UI_BTN_LEFT);   CHECK(s.screen == UI_TOLERANCE);
  s = uiStep(s, UI_BTN_LEFT);   CHECK(s.screen == UI_SETPOINT);
  s = uiStep(s, UI_BTN_LEFT);   CHECK(s.screen == UI_HOME);

  // --- Select returns to Home from any screen --------------------------------
  s = uiInitial();
  s = uiStep(s, UI_BTN_RIGHT); s = uiStep(s, UI_BTN_RIGHT);   // now on Tolerance
  CHECK(s.screen == UI_TOLERANCE);
  s = uiStep(s, UI_BTN_SELECT);  CHECK(s.screen == UI_HOME);
  s = uiStep(s, UI_BTN_RIGHT); s = uiStep(s, UI_BTN_RIGHT); s = uiStep(s, UI_BTN_RIGHT);
  CHECK(s.screen == UI_STATS);
  s = uiStep(s, UI_BTN_SELECT);  CHECK(s.screen == UI_HOME);

  // --- Setpoint editing: 0.5 C steps -----------------------------------------
  s = uiInitial();
  s = uiStep(s, UI_BTN_RIGHT);  CHECK(s.screen == UI_SETPOINT);
  s = uiStep(s, UI_BTN_UP);     CHECK(near(s.setpointC, 24.5f));
  s = uiStep(s, UI_BTN_UP);     CHECK(near(s.setpointC, 25.0f));
  s = uiStep(s, UI_BTN_DOWN);   CHECK(near(s.setpointC, 24.5f));
  // Navigation must not disturb the edited value.
  s = uiStep(s, UI_BTN_LEFT);   CHECK(s.screen == UI_HOME);
  CHECK(near(s.setpointC, 24.5f));

  // --- Setpoint clamps at both ends (edits at the clamp don't overshoot) -----
  s = uiInitial();
  s = uiStep(s, UI_BTN_RIGHT);                        // Setpoint screen
  for (int i = 0; i < 40; ++i) s = uiStep(s, UI_BTN_UP);
  CHECK(near(s.setpointC, 30.0f));                    // clamps at max 30 C
  s = uiStep(s, UI_BTN_UP);   CHECK(near(s.setpointC, 30.0f));  // stays clamped
  for (int i = 0; i < 40; ++i) s = uiStep(s, UI_BTN_DOWN);
  CHECK(near(s.setpointC, 18.0f));                    // clamps at min 18 C
  s = uiStep(s, UI_BTN_DOWN); CHECK(near(s.setpointC, 18.0f));  // stays clamped

  // --- Tolerance editing: 0.25 C steps ---------------------------------------
  s = uiInitial();
  s = uiStep(s, UI_BTN_RIGHT); s = uiStep(s, UI_BTN_RIGHT);    // Tolerance screen
  CHECK(s.screen == UI_TOLERANCE);
  s = uiStep(s, UI_BTN_UP);     CHECK(near(s.toleranceC, 0.75f));
  s = uiStep(s, UI_BTN_DOWN);   CHECK(near(s.toleranceC, 0.5f));
  s = uiStep(s, UI_BTN_DOWN);   CHECK(near(s.toleranceC, 0.25f));
  // Editing Tolerance must not touch the Setpoint.
  CHECK(near(s.setpointC, 24.0f));

  // --- Tolerance clamps at both ends -----------------------------------------
  s = uiInitial();
  s = uiStep(s, UI_BTN_RIGHT); s = uiStep(s, UI_BTN_RIGHT);
  for (int i = 0; i < 20; ++i) s = uiStep(s, UI_BTN_UP);
  CHECK(near(s.toleranceC, 2.0f));                    // clamps at max +/-2.0 C
  s = uiStep(s, UI_BTN_UP);   CHECK(near(s.toleranceC, 2.0f));
  for (int i = 0; i < 20; ++i) s = uiStep(s, UI_BTN_DOWN);
  CHECK(near(s.toleranceC, 0.25f));                   // clamps at min +/-0.25 C
  s = uiStep(s, UI_BTN_DOWN); CHECK(near(s.toleranceC, 0.25f));

  // --- Home Up/Down shortcut edits the Setpoint (same clamping) --------------
  s = uiInitial();
  CHECK(s.screen == UI_HOME);
  s = uiStep(s, UI_BTN_UP);     CHECK(near(s.setpointC, 24.5f));  // edits Setpoint from Home
  s = uiStep(s, UI_BTN_DOWN);   CHECK(near(s.setpointC, 24.0f));
  CHECK(s.screen == UI_HOME);                         // shortcut doesn't leave Home
  for (int i = 0; i < 40; ++i) s = uiStep(s, UI_BTN_DOWN);
  CHECK(near(s.setpointC, 18.0f));                    // Home shortcut clamps too
  for (int i = 0; i < 40; ++i) s = uiStep(s, UI_BTN_UP);
  CHECK(near(s.setpointC, 30.0f));

  // --- Stats is a read-only shell: Up/Down change nothing --------------------
  s = uiInitial();
  s = uiStep(s, UI_BTN_LEFT);   CHECK(s.screen == UI_STATS);  // wrap straight to Stats
  s = uiStep(s, UI_BTN_UP);     CHECK(near(s.setpointC, 24.0f));
  s = uiStep(s, UI_BTN_DOWN);   CHECK(near(s.toleranceC, 0.5f));
  CHECK(s.screen == UI_STATS);

  // --- A none-event is a no-op -----------------------------------------------
  s = uiInitial();
  UiState before = s;
  s = uiStep(s, UI_BTN_NONE);
  CHECK(s.screen == before.screen);
  CHECK(near(s.setpointC, before.setpointC));
  CHECK(near(s.toleranceC, before.toleranceC));

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
