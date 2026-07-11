#ifndef DOUGH_RAY_ME_HISTORY_H
#define DOUGH_RAY_ME_HISTORY_H

#include <stdint.h>
#include <math.h>

// Pure history model behind the Graph screen for dough-ray-me: the last 80
// minutes of the Fermenting Box sliced into 16 windows of 5 minutes each, so the
// baker can see how the box got here rather than just where it is now. Like
// stats.h / control.h / safety.h it has no Arduino, LCD, OneWire, or millis()
// dependency, so it builds and runs on the host for testing. All state lives in a
// HistoryState the caller threads in and out (value semantics) -- the .ino owns
// the single instance and, on the shield's physical RESET, the Arduino reboots
// and this starts fresh at historyInitial() (RAM-only) while the Setpoint /
// Tolerance survive in EEPROM, the same lifecycle as the Stats screen.
//
// It mirrors stats.h's two-fold shape, per 5-minute window:
//   * historyObserveTemp -- folds a *valid* Box Air Temperature reading into the
//     current window's running mean; invalid / faulted readings are not folded,
//     exactly as stats.h only observes valid readings.
//   * historyAccrue -- folds elapsed milliseconds and the *actual* heater relay
//     state into the current window's Heater Duty, and drives the 5-minute window
//     rollover. Elapsed time is threaded in, never read from a clock here
//     (ADR-0002), and accrues against the real relay state so a Safety Cutoff or
//     Sensor Fault that forces the bulb OFF counts as OFF.
//   * historyRender -- given the current Setpoint, returns a per-column render
//     model the .ino can draw without any arithmetic of its own: a Setpoint-
//     centered bar level and a Heater-Duty digit, newest column on the right.

// The three state-threading functions below carry a HistoryState by value (228
// bytes at 16 columns). In the Arduino sketch the whole program collapses into a
// single inlined main(), and gcc gives each inlined by-value temporary its own
// stack slot rather than reusing one -- so left to inline, the accrue/observe/
// render copies *sum* into main()'s frame (~1.2 KB) and, on top of the ~0.9 KB of
// globals, overflow the ATmega328P's 2 KB SRAM: the stack lands on top of the
// globals at boot and the board resets before it prints its first serial line.
// Marking these noinline keeps each big copy in a short-lived frame that is freed
// on return, so the peak stack stays bounded. HISTORY_NOINLINE is a no-op on the
// host, where the tests compile this header natively. See docs/adr for the SRAM
// budget note; do not "simplify" these back to plain inline without re-checking
// the sketch's main() frame size (avr-objdump -d, main's subi r28/sbci r29).
#if defined(__AVR__)
#define HISTORY_NOINLINE __attribute__((noinline))
#else
#define HISTORY_NOINLINE
#endif

static const int           HISTORY_COLUMNS   = 16;         // 16 windows on screen
static const unsigned long HISTORY_WINDOW_MS = 300000UL;   // 5 minutes per window
static const float         HISTORY_BAR_STEP  = 0.5f;       // deg C per bar level
static const int           HISTORY_BAR_MAX   = 4;          // +/-4 levels = +/-2 deg C
static const int           HISTORY_EMPTY     = -128;       // render sentinel: no data,
                                                           // distinct from any bar
                                                           // level (-4..+4) or duty
                                                           // digit (0..9)

// One accumulated window. tempCount == 0 means no valid reading was folded (a
// never-touched window, or one the probe was faulted through), so its bar renders
// EMPTY -- kept distinct from a genuine cold reading.
struct HistoryWindow {
  float         tempSum;     // sum of valid Box Air Temperature readings this window
  uint16_t      tempCount;   // number of valid readings folded (0 => bar EMPTY)
  unsigned long heaterOnMs;  // ms the heater relay was ON within this window
  unsigned long windowMs;    // ms accrued into this window (rolls at HISTORY_WINDOW_MS)
};

// The full history as a ring of windows. `head` is the current (newest, in-
// progress) window; `count` is how many windows have been started, capped at
// HISTORY_COLUMNS -- older windows past the cap have dropped off the left.
struct HistoryState {
  HistoryWindow cols[HISTORY_COLUMNS];
  int           head;
  int           count;
};

// Per-column render model: two small integers so the .ino does no arithmetic.
// barLevel is -HISTORY_BAR_MAX..+HISTORY_BAR_MAX or HISTORY_EMPTY; dutyDigit is
// 0..9 or HISTORY_EMPTY (EMPTY only for a never-started column).
struct HistoryColumnRender {
  int barLevel;
  int dutyDigit;
};

// The whole 16-column render, oldest on the left (index 0), newest on the right
// (index HISTORY_COLUMNS - 1), matching how the Graph screen scrolls.
struct HistoryRender {
  HistoryColumnRender cols[HISTORY_COLUMNS];
};

inline HistoryWindow historyWindowInitial() {
  HistoryWindow w;
  w.tempSum    = 0.0f;
  w.tempCount  = 0;
  w.heaterOnMs = 0;
  w.windowMs   = 0;
  return w;
}

// Fresh, since-power-on history: one in-progress window (the live right edge) and
// fifteen blank columns to its left. count == 1 so power-on honestly shows only
// the rightmost column populated, filling in from the right as the bake proceeds.
inline HistoryState historyInitial() {
  HistoryState s;
  for (int i = 0; i < HISTORY_COLUMNS; ++i) s.cols[i] = historyWindowInitial();
  s.head  = 0;
  s.count = 1;
  return s;
}

// Freeze the current window and open a fresh in-progress one to its right. The
// ring advances; once count reaches HISTORY_COLUMNS the oldest window is
// overwritten (drops off the left).
inline HistoryState historyRollover(HistoryState s) {
  s.head = (s.head + 1) % HISTORY_COLUMNS;
  s.cols[s.head] = historyWindowInitial();
  if (s.count < HISTORY_COLUMNS) ++s.count;
  return s;
}

// Fold a valid Box Air Temperature reading into the current window's running
// mean. Invalid / faulted readings are simply not passed here (as in stats.h).
HISTORY_NOINLINE inline HistoryState historyObserveTemp(HistoryState s, float tempC) {
  s.cols[s.head].tempSum += tempC;
  s.cols[s.head].tempCount += 1;
  return s;
}

// Accrue an elapsed interval into the current window's Heater Duty, against the
// actual relay state, and roll the window over at exactly 5 minutes. An interval
// that would cross a boundary is split so elapsed time accumulates across the
// rollover rather than being lost or double-counted; the loop is robust to a
// single interval spanning several windows.
HISTORY_NOINLINE inline HistoryState historyAccrue(HistoryState s, bool heatOn, unsigned long deltaMs) {
  while (deltaMs > 0) {
    // Invariant at the top of the loop: the current window is not yet full, so
    // there is at least 1 ms of room and the loop makes progress.
    unsigned long room = HISTORY_WINDOW_MS - s.cols[s.head].windowMs;
    unsigned long take = deltaMs < room ? deltaMs : room;
    s.cols[s.head].windowMs += take;
    if (heatOn) s.cols[s.head].heaterOnMs += take;
    deltaMs -= take;
    if (s.cols[s.head].windowMs >= HISTORY_WINDOW_MS) s = historyRollover(s);
  }
  return s;
}

// Map a window's mean Box Air Temperature to a Setpoint-centered bar level:
// deviation from the Setpoint at HISTORY_BAR_STEP (0.5 deg C) per level, rounded,
// clipped to +/-HISTORY_BAR_MAX. On-Setpoint renders level 0; beyond +/-2 deg C
// clips to +/-4. A window with no valid samples renders EMPTY.
inline int historyBarLevel(HistoryWindow w, float setpointC) {
  if (w.tempCount == 0) return HISTORY_EMPTY;
  float mean = w.tempSum / (float)w.tempCount;
  int level = (int)lroundf((mean - setpointC) / HISTORY_BAR_STEP);
  if (level >  HISTORY_BAR_MAX) level =  HISTORY_BAR_MAX;
  if (level < -HISTORY_BAR_MAX) level = -HISTORY_BAR_MAX;
  return level;
}

// A window's Heater Duty as a single digit 0..9 (duty in tenths, rounded), with a
// 100 %-on window clamped to 9 and a 0 %-on window reading 0. Before any time has
// accrued the digit is 0. The multiply is widened to 64-bit so a full window of
// millis() math can't overflow.
inline int historyDutyDigit(HistoryWindow w) {
  if (w.windowMs == 0) return 0;
  uint64_t tenths = ((uint64_t)w.heaterOnMs * 10 + w.windowMs / 2) / w.windowMs;
  return tenths > 9 ? 9 : (int)tenths;
}

// Build the per-column render model against the current Setpoint. Started windows
// fill from the right (newest at HISTORY_COLUMNS - 1); columns never started
// render fully EMPTY so "no data yet" is never mistaken for a real cold reading.
HISTORY_NOINLINE inline HistoryRender historyRender(const HistoryState& s, float setpointC) {
  HistoryRender r;
  for (int d = 0; d < HISTORY_COLUMNS; ++d) {
    r.cols[d].barLevel  = HISTORY_EMPTY;
    r.cols[d].dutyDigit = HISTORY_EMPTY;
  }
  for (int k = 0; k < s.count; ++k) {
    int display = HISTORY_COLUMNS - 1 - k;                 // newest at the right
    int ring    = (s.head - k + HISTORY_COLUMNS) % HISTORY_COLUMNS;
    r.cols[display].barLevel  = historyBarLevel(s.cols[ring], setpointC);
    r.cols[display].dutyDigit = historyDutyDigit(s.cols[ring]);
  }
  return r;
}

#endif  // DOUGH_RAY_ME_HISTORY_H
