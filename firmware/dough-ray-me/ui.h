#ifndef DOUGH_RAY_ME_UI_H
#define DOUGH_RAY_ME_UI_H

// Pure UI state machine for dough-ray-me: (state, buttonEvent) -> state. No
// Arduino, LCD, or keypad dependencies, so it builds and runs on the host for
// testing. The impure parts -- decoding the A0 analog ladder into button events
// with edge detection and auto-repeat timing, and painting the LCD -- live in
// the .ino; this unit only moves through the four screens and edits the live
// Setpoint / Tolerance the control law consumes.
//
// Ranges and steps come from CONTEXT.md / SPEC.md:
//   Setpoint  18.0-30.0 C in 0.5  C steps (boots to 24.0)
//   Tolerance  0.25-2.0 C in 0.25 C steps (boots to +/-0.5), a +/- half-band.

// Boot defaults and edit limits. These are the ubiquitous-language numbers, not
// implementation knobs -- the host tests derive their expectations from them.
const float UI_SETPOINT_MIN   = 18.0f;
const float UI_SETPOINT_MAX   = 30.0f;
const float UI_SETPOINT_STEP  = 0.5f;
const float UI_SETPOINT_BOOT  = 24.0f;

const float UI_TOLERANCE_MIN  = 0.25f;
const float UI_TOLERANCE_MAX  = 2.0f;
const float UI_TOLERANCE_STEP = 0.25f;
const float UI_TOLERANCE_BOOT = 0.5f;

// The four screens, paged with Left/Right and wrapping:
//   Home -> Setpoint -> Tolerance -> Stats -> (wraps back to Home)
enum UiScreen {
  UI_HOME = 0,      // read-only: Box Air Temperature, Setpoint, heat state, Tolerance
  UI_SETPOINT,      // Up/Down edit the Setpoint
  UI_TOLERANCE,     // Up/Down edit the Tolerance
  UI_STATS,         // read-only: min/max Box Air Temperature + Heater Duty
  UI_SCREEN_COUNT   // sentinel = number of screens, used for wrap arithmetic
};

// Button events fed in by the keypad decode. Auto-repeat is done upstream by
// millis() timing, so a held Up/Down simply arrives as repeated UI_BTN_UP /
// UI_BTN_DOWN events -- this pure unit treats each one as a single step.
enum UiButton {
  UI_BTN_NONE = 0,
  UI_BTN_UP,
  UI_BTN_DOWN,
  UI_BTN_LEFT,
  UI_BTN_RIGHT,
  UI_BTN_SELECT
};

struct UiState {
  UiScreen screen;
  float    setpointC;
  float    toleranceC;
};

inline UiState uiInitial() {
  UiState s;
  s.screen     = UI_HOME;
  s.setpointC  = UI_SETPOINT_BOOT;
  s.toleranceC = UI_TOLERANCE_BOOT;
  return s;
}

inline float uiClamp(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// One transition. Navigation (Left/Right/Select) never changes the values;
// Up/Down edit the current screen's value live and clamp at both ends. On Home,
// Up/Down are a shortcut for the Setpoint -- the baker's most common action.
inline UiState uiStep(UiState s, UiButton ev) {
  switch (ev) {
    case UI_BTN_LEFT:
      s.screen = (UiScreen)((s.screen + UI_SCREEN_COUNT - 1) % UI_SCREEN_COUNT);
      break;
    case UI_BTN_RIGHT:
      s.screen = (UiScreen)((s.screen + 1) % UI_SCREEN_COUNT);
      break;
    case UI_BTN_SELECT:
      s.screen = UI_HOME;                 // reliable escape back to Home from anywhere
      break;
    case UI_BTN_UP:
      if (s.screen == UI_TOLERANCE) {
        s.toleranceC = uiClamp(s.toleranceC + UI_TOLERANCE_STEP,
                               UI_TOLERANCE_MIN, UI_TOLERANCE_MAX);
      } else if (s.screen == UI_HOME || s.screen == UI_SETPOINT) {
        s.setpointC = uiClamp(s.setpointC + UI_SETPOINT_STEP,
                              UI_SETPOINT_MIN, UI_SETPOINT_MAX);
      }
      break;                              // Stats has no editable value
    case UI_BTN_DOWN:
      if (s.screen == UI_TOLERANCE) {
        s.toleranceC = uiClamp(s.toleranceC - UI_TOLERANCE_STEP,
                               UI_TOLERANCE_MIN, UI_TOLERANCE_MAX);
      } else if (s.screen == UI_HOME || s.screen == UI_SETPOINT) {
        s.setpointC = uiClamp(s.setpointC - UI_SETPOINT_STEP,
                              UI_SETPOINT_MIN, UI_SETPOINT_MAX);
      }
      break;
    case UI_BTN_NONE:
    default:
      break;                              // no event, no change
  }
  return s;
}

#endif  // DOUGH_RAY_ME_UI_H
