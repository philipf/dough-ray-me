#ifndef DOUGH_RAY_ME_PERSIST_H
#define DOUGH_RAY_ME_PERSIST_H

// Pure persistence logic for dough-ray-me: decides *when* the baker's Setpoint
// and Tolerance should be committed to EEPROM, and *what* to trust when reading
// them back on boot. Like control.h / safety.h / ui.h it has no Arduino or
// EEPROM dependency -- the .ino owns the real EEPROM.get/put and millis(); this
// unit only carries the debounce state machine and the boot validity check, so
// both build and run on the host for testing.
//
// Two jobs:
//   * persistStep(...) -- debounce writes. A change to either value (re)starts a
//     ~2 s timer; only once the values have held *steady* for that long does it
//     ask for a single write. Holding Up/Down to ramp restarts the timer on
//     every step, so a whole ramp costs one write, not one per step -- sparing
//     the EEPROM's limited erase/write cycles.
//   * persistDecode(...) -- validity on boot. The EEPROM read-back is trusted
//     only when a magic marker matches and both values sit inside their
//     documented ranges; anything else (a first-ever flash, or garbage) falls
//     back to the safe defaults (Setpoint 24 C, Tolerance +/-0.5 C).
//
// The debounce state is threaded in and out as PersistState -- the committed
// values plus the pending timer -- exactly as decideHeat() threads currentHeat
// and safetyGate() threads overTempLatched: no module globals, and time is a
// parameter, never read inside.

#include <stdint.h>

#include "ui.h"   // UI_SETPOINT_* / UI_TOLERANCE_* ranges + boot defaults

// How long a value must hold steady before it is written back (SPEC.md: ~2 s).
const unsigned long PERSIST_DEBOUNCE_MS = 2000;

// Marker stored ahead of the values so an uninitialised EEPROM (a fresh chip
// reads back as 0xFF bytes) is never mistaken for saved settings. Bump this if
// the stored layout ever changes.
const uint8_t PERSIST_MAGIC = 0x24;

// Debounce state, threaded call to call. committed* is what EEPROM currently
// holds; pending* is the value seen at the previous step, used to tell a
// still-moving ramp (restart the timer) from a settled one (let it fall due).
struct PersistState {
  float         committedSetpointC;
  float         committedToleranceC;
  float         pendingSetpointC;
  float         pendingToleranceC;
  bool          pending;      // a change is waiting out its debounce window
  unsigned long dueMs;        // when the pending write falls due (millis stamp)
};

// Result of one step: the next state, plus whether the caller should write the
// committed values out to EEPROM *now* (a one-shot request).
struct PersistUpdate {
  PersistState state;
  bool         write;
};

// Seed the debounce state from the values just loaded on boot, so committed
// matches EEPROM and no spurious write fires until the baker actually changes
// something.
inline PersistState persistInitial(float setpointC, float toleranceC) {
  PersistState s;
  s.committedSetpointC  = setpointC;
  s.committedToleranceC = toleranceC;
  s.pendingSetpointC    = setpointC;
  s.pendingToleranceC   = toleranceC;
  s.pending             = false;
  s.dueMs               = 0;
  return s;
}

// One debounce step. Fed the live Setpoint/Tolerance and the current millis()
// stamp; returns the next state and a one-shot write request. Values on the
// 0.5 / 0.25 grid come straight from uiStep(), so exact float equality is the
// right change test here (cf. the LCD sentinel pattern).
inline PersistUpdate persistStep(PersistState s, float setpointC,
                                 float toleranceC, unsigned long now) {
  PersistUpdate out;
  out.write = false;

  bool matchesCommitted = (setpointC == s.committedSetpointC) &&
                          (toleranceC == s.committedToleranceC);

  if (matchesCommitted) {
    // Back at (or never left) what EEPROM holds -- nothing to write. Cancel any
    // armed timer so a change-then-change-back doesn't leave a write pending.
    s.pending = false;
  } else if (!s.pending || setpointC != s.pendingSetpointC ||
             toleranceC != s.pendingToleranceC) {
    // A fresh change (or the next ramp step): (re)start the debounce window. The
    // write waits until the value stops moving for PERSIST_DEBOUNCE_MS.
    s.pending           = true;
    s.pendingSetpointC  = setpointC;
    s.pendingToleranceC = toleranceC;
    s.dueMs             = now + PERSIST_DEBOUNCE_MS;
  } else if ((long)(now - s.dueMs) >= 0) {
    // Held steady past the window: commit both values and ask for the write.
    // Wrap-safe millis() comparison (ADR-0002).
    s.committedSetpointC  = setpointC;
    s.committedToleranceC = toleranceC;
    s.pending             = false;
    out.write             = true;
  }
  // else: still pending, timer not yet due -- keep waiting.

  out.state = s;
  return out;
}

// Boot-time read-back values.
struct PersistValues {
  float setpointC;
  float toleranceC;
};

// True when v sits on the editable grid (min + n*step) within a tolerance far
// tighter than a step but far looser than float round-off. uiStep() only ever
// produces on-grid values, so an in-range but *off*-grid read-back means the
// bytes are stale (a prior firmware with different steps) or corrupt -- not a
// value this firmware could have written. A NaN v makes every term NaN and so
// fails the comparison, joining the range check in rejecting garbage.
inline bool persistOnGrid(float v, float min, float step) {
  float steps   = (v - min) / step;               // v >= min already checked
  long  n       = (long)(steps + 0.5f);           // nearest grid index
  float snapped = min + (float)n * step;
  float diff    = snapped - v;
  if (diff < 0) diff = -diff;
  return diff <= 0.01f;                            // << 0.25 step, >> float error
}

// Trust the EEPROM values only when the magic marker matches and both sit inside
// their documented editable ranges *and on their grids*; otherwise fall back to
// the safe defaults. A NaN read (garbage float) fails every comparison, so it too
// resolves to the defaults rather than driving the control law with nonsense.
inline PersistValues persistDecode(uint8_t magic, float setpointC,
                                   float toleranceC) {
  bool valid = (magic == PERSIST_MAGIC) &&
               (setpointC  >= UI_SETPOINT_MIN)  && (setpointC  <= UI_SETPOINT_MAX) &&
               (toleranceC >= UI_TOLERANCE_MIN) && (toleranceC <= UI_TOLERANCE_MAX) &&
               persistOnGrid(setpointC,  UI_SETPOINT_MIN,  UI_SETPOINT_STEP) &&
               persistOnGrid(toleranceC, UI_TOLERANCE_MIN, UI_TOLERANCE_STEP);
  PersistValues v;
  v.setpointC  = valid ? setpointC  : UI_SETPOINT_BOOT;
  v.toleranceC = valid ? toleranceC : UI_TOLERANCE_BOOT;
  return v;
}

#endif  // DOUGH_RAY_ME_PERSIST_H
