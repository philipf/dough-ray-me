// Host tests for the pure Graph-screen history model. Dependency-free: a tiny
// CHECK macro, compiled and run natively (see run.sh). Expected values come from
// the Graph screen definition in CONTEXT.md and issue #10/#11 (16 windows of
// 5 minutes, per-window mean Box Air Temperature as a Setpoint-centered bar, and
// per-window Heater Duty in tenths), not from re-deriving the implementation. The
// CGRAM glyphs, LCD writes, and the millis() timing that feeds elapsed intervals
// are impure glue in the .ino; only the pure model is exercised here.
#include <cstdio>
#include "../dough-ray-me/history.h"

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

// The rightmost (newest) column is the live, in-progress window.
static HistoryColumnRender newest(HistoryRender r) {
  return r.cols[HISTORY_COLUMNS - 1];
}

int main() {
  const unsigned long W = HISTORY_WINDOW_MS;   // one 5-minute window in ms

  // --- Fresh state: only the rightmost column exists, and it is empty ---------
  // On power-on the graph honestly shows no history: the live right edge is
  // present but has no samples yet, every other column is blank.
  {
    HistoryState s = historyInitial();
    HistoryRender r = historyRender(s, 24.0f);
    for (int d = 0; d < HISTORY_COLUMNS - 1; ++d) {
      CHECK(r.cols[d].barLevel  == HISTORY_EMPTY);   // 15 blank columns...
      CHECK(r.cols[d].dutyDigit == HISTORY_EMPTY);   // ...blank on both rows
    }
    CHECK(newest(r).barLevel  == HISTORY_EMPTY);     // no valid sample yet -> EMPTY bar
    CHECK(newest(r).dutyDigit == 0);                 // no time accrued yet -> 0 duty
  }

  // --- Window mean: several readings average into one bar --------------------
  // Setpoint 24; a window whose readings mean to 25.0 sits +1.0 C = +2 levels.
  {
    HistoryState s = historyInitial();
    s = historyObserveTemp(s, 24.0f);
    s = historyObserveTemp(s, 26.0f);   // mean 25.0
    HistoryRender r = historyRender(s, 24.0f);
    CHECK(newest(r).barLevel == 2);     // +1.0 C / 0.5 = +2
  }

  // --- On-Setpoint window renders the flat mid-line (level 0) -----------------
  // The exact on-Setpoint boundary, plus a hold within a quarter degree, both
  // read as level 0; a half-degree excursion is the first ripple.
  {
    HistoryState s = historyInitial();
    s = historyObserveTemp(s, 24.0f);           // dead on Setpoint
    CHECK(newest(historyRender(s, 24.0f)).barLevel == 0);

    s = historyInitial();
    s = historyObserveTemp(s, 24.2f);           // +0.2 C, inside the flat band
    CHECK(newest(historyRender(s, 24.0f)).barLevel == 0);

    s = historyInitial();
    s = historyObserveTemp(s, 23.8f);           // -0.2 C, inside the flat band
    CHECK(newest(historyRender(s, 24.0f)).barLevel == 0);

    s = historyInitial();
    s = historyObserveTemp(s, 24.5f);           // +0.5 C -> first ripple up
    CHECK(newest(historyRender(s, 24.0f)).barLevel == 1);
    s = historyInitial();
    s = historyObserveTemp(s, 23.5f);           // -0.5 C -> first ripple down
    CHECK(newest(historyRender(s, 24.0f)).barLevel == -1);
  }

  // --- Bar scale: 0.5 C per level, and clipping beyond +/-2 C -----------------
  {
    HistoryState s;
    // Exact full-scale edges map to +/-4, not beyond.
    s = historyInitial(); s = historyObserveTemp(s, 26.0f);   // +2.0 C
    CHECK(newest(historyRender(s, 24.0f)).barLevel == 4);
    s = historyInitial(); s = historyObserveTemp(s, 22.0f);   // -2.0 C
    CHECK(newest(historyRender(s, 24.0f)).barLevel == -4);
    // Beyond +/-2 C pegs the top / bottom level (a cold start reads "off the edge").
    s = historyInitial(); s = historyObserveTemp(s, 30.0f);   // +6.0 C
    CHECK(newest(historyRender(s, 24.0f)).barLevel == 4);
    s = historyInitial(); s = historyObserveTemp(s, 18.0f);   // -6.0 C
    CHECK(newest(historyRender(s, 24.0f)).barLevel == -4);
    // Intermediate levels at 0.5 C steps.
    s = historyInitial(); s = historyObserveTemp(s, 25.0f);   // +1.0 -> +2
    CHECK(newest(historyRender(s, 24.0f)).barLevel == 2);
    s = historyInitial(); s = historyObserveTemp(s, 25.5f);   // +1.5 -> +3
    CHECK(newest(historyRender(s, 24.0f)).barLevel == 3);
  }

  // --- Re-centering: one stored history, two Setpoints, shifted bars ----------
  // The bar is deviation from the *current* Setpoint, so changing the Setpoint
  // shifts every level correspondingly (story #9).
  {
    HistoryState s = historyInitial();
    s = historyObserveTemp(s, 25.0f);          // a window whose mean is 25.0
    CHECK(newest(historyRender(s, 24.0f)).barLevel == 2);   // +1.0 vs SP 24
    CHECK(newest(historyRender(s, 25.0f)).barLevel == 0);   //  0.0 vs SP 25
    CHECK(newest(historyRender(s, 26.0f)).barLevel == -2);  // -1.0 vs SP 26
  }

  // --- Heater Duty digit: 0 %, 100 % clamp, and rounding to tenths ------------
  {
    HistoryState s;
    // Whole window OFF -> 0.
    s = historyInitial(); s = historyAccrue(s, false, W);
    CHECK(newest(historyRender(s, 24.0f)).dutyDigit == 0);
    // Whole window ON -> would be 10 tenths, clamped to 9.
    s = historyInitial(); s = historyAccrue(s, true, W);
    // A full window rolls over, so the completed window is second-from-right.
    CHECK(historyRender(s, 24.0f).cols[HISTORY_COLUMNS - 2].dutyDigit == 9);
    // 30 % ON -> digit 3.
    s = historyInitial();
    s = historyAccrue(s, true,  (unsigned long)(W * 0.3));
    s = historyAccrue(s, false, W - (unsigned long)(W * 0.3));
    CHECK(historyRender(s, 24.0f).cols[HISTORY_COLUMNS - 2].dutyDigit == 3);
    // 95 % ON rounds to 10 tenths, still clamped to 9 (never rolls to 0).
    s = historyInitial();
    s = historyAccrue(s, true,  (unsigned long)(W * 0.95));
    s = historyAccrue(s, false, W - (unsigned long)(W * 0.95));
    CHECK(historyRender(s, 24.0f).cols[HISTORY_COLUMNS - 2].dutyDigit == 9);
  }

  // --- Duty counts the actual relay state (safety-forced OFF counts as OFF) ---
  // Half the window the heater is forced OFF (e.g. Safety Cutoff): duty is 5.
  {
    HistoryState s = historyInitial();
    s = historyAccrue(s, true,  W / 2);        // 2.5 min actually ON
    s = historyAccrue(s, false, W / 2);        // 2.5 min forced OFF
    CHECK(historyRender(s, 24.0f).cols[HISTORY_COLUMNS - 2].dutyDigit == 5);
  }

  // --- Window rolls over at exactly 5 min; time accumulates across rollovers --
  // Accruing just under a window keeps one live column; the extra ms that crosses
  // 5 min freezes it and opens the next, with the remainder landing in the new
  // window (elapsed time is not lost at the boundary).
  {
    HistoryState s = historyInitial();
    s = historyAccrue(s, true, W - 1000);      // 4 min 59 s, still in-progress
    CHECK(s.count == 1);                        // no rollover yet
    CHECK(s.cols[s.head].windowMs == W - 1000);
    s = historyAccrue(s, true, 3000);          // crosses the 5-min boundary by 2 s
    CHECK(s.count == 2);                        // exactly one rollover
    CHECK(s.cols[s.head].windowMs == 2000);    // remainder carried into new window
  }
  {
    // A single interval spanning several windows rolls over the right number of
    // times and keeps the leftover (robustness of the split loop).
    HistoryState s = historyInitial();
    s = historyAccrue(s, true, W * 3 + 1234);  // three whole windows + 1234 ms
    CHECK(s.count == 4);                        // 3 rollovers -> 4 windows started
    CHECK(s.cols[s.head].windowMs == 1234);
  }

  // --- A whole-window-faulted column and a never-observed column both EMPTY ----
  // ...and both are distinct from a genuine cold reading (level -4).
  {
    HistoryState s = historyInitial();
    // Window 0: time accrues but the probe is faulted the whole time -> no temps.
    s = historyAccrue(s, true, W);             // rolls over; window 0 has duty but
                                               // no valid samples
    HistoryRender r = historyRender(s, 24.0f);
    HistoryColumnRender faulted = r.cols[HISTORY_COLUMNS - 2];  // the faulted window
    HistoryColumnRender never   = r.cols[0];                    // a never-started column
    CHECK(faulted.barLevel  == HISTORY_EMPTY); // faulted -> empty bar, not a temperature
    CHECK(faulted.dutyDigit != HISTORY_EMPTY); // ...but its Heater Duty is real
    CHECK(never.barLevel    == HISTORY_EMPTY); // never observed -> empty bar
    CHECK(never.dutyDigit   == HISTORY_EMPTY); // ...and empty duty (fully blank)
    CHECK(HISTORY_EMPTY != -HISTORY_BAR_MAX);  // EMPTY is not the genuine cold level
  }

  // --- Safety Cutoff scar: a fired 35 C cutoff latches a ! over the duty digit --
  // The full-screen Alarm only shows while the cutoff is active (ADR-0001); once
  // the box re-arms it clears and nothing else records the excursion. The Graph
  // keeps a permanent scar: any window in which the Safety Cutoff fired renders a
  // CUTOFF sentinel in its duty slot, ahead of (and replacing) the tenths digit.
  {
    // A cutoff firing within a window latches the scar, and it takes precedence
    // over the real Heater Duty digit and survives the box recovering / re-arming.
    HistoryState s = historyInitial();
    s = historyAccrue(s, false, W / 4, true);   // over-temp: heater forced OFF, cutoff firing
    CHECK(newest(historyRender(s, 24.0f)).dutyDigit == HISTORY_CUTOFF);
    s = historyAccrue(s, true, W / 4, false);   // re-armed: heater back ON, no cutoff
    // Real duty has accrued, but the scar still wins -- CUTOFF over the digit.
    CHECK(newest(historyRender(s, 24.0f)).dutyDigit == HISTORY_CUTOFF);
    // The scar is a distinct sentinel, never confused with EMPTY or a duty digit.
    CHECK(HISTORY_CUTOFF != HISTORY_EMPTY);
  }
  {
    // A Sensor Fault also forces the heater OFF, but it is NOT a Safety Cutoff --
    // no scar. The window renders its real Heater Duty (0 here), never CUTOFF.
    HistoryState s = historyInitial();
    s = historyAccrue(s, false, W, false);      // whole window OFF, but no over-temp
    CHECK(historyRender(s, 24.0f).cols[HISTORY_COLUMNS - 2].dutyDigit == 0);
  }
  {
    // The scar latches on its window and survives the 5-minute rollover: once
    // frozen it keeps reading CUTOFF as a past column, a permanent timeline scar,
    // while the fresh live window opens clean.
    HistoryState s = historyInitial();
    s = historyAccrue(s, true, W, true);        // a full window with the cutoff firing -> rolls over
    HistoryRender r = historyRender(s, 24.0f);
    CHECK(r.cols[HISTORY_COLUMNS - 2].dutyDigit == HISTORY_CUTOFF);  // frozen window keeps the scar
    CHECK(newest(r).dutyDigit != HISTORY_CUTOFF);                    // fresh live window is clean
  }
  {
    // A cutoff interval spanning a window boundary scars both windows it touched.
    HistoryState s = historyInitial();
    s = historyAccrue(s, false, W + W / 2, true);   // 1.5 windows, cutoff throughout
    HistoryRender r = historyRender(s, 24.0f);
    CHECK(r.cols[HISTORY_COLUMNS - 2].dutyDigit == HISTORY_CUTOFF);  // frozen window scarred
    CHECK(newest(r).dutyDigit == HISTORY_CUTOFF);                    // live window scarred too
  }

  // --- Ring order: newest on the right, oldest drops off after 16 windows ------
  // Give each window a distinct mean by folding one reading per window, then
  // rolling over. Setpoint is chosen so the level equals a clean index marker.
  {
    HistoryState s = historyInitial();
    // Fill 16 full windows with means 24.0, 24.5, 25.0, ... one level apart-ish,
    // then two more to push the oldest two off the left.
    for (int i = 0; i < 18; ++i) {
      s = historyObserveTemp(s, 24.0f + 0.5f * (float)i);  // window i's mean
      s = historyAccrue(s, false, W);                      // freeze & advance
    }
    // 18 windows started but only the last 16 survive; the current (19th) window
    // is the empty live edge on the right.
    CHECK(s.count == HISTORY_COLUMNS);
    HistoryRender r = historyRender(s, 24.0f);
    // Rightmost is the fresh live window (no samples yet).
    CHECK(newest(r).barLevel == HISTORY_EMPTY);
    // Second-from-right is the most recently frozen window (i == 17, mean 32.5,
    // far above Setpoint -> clipped to +4).
    CHECK(r.cols[HISTORY_COLUMNS - 2].barLevel == 4);
    // The two oldest windows (i == 0, 1) have dropped off; every visible column
    // holds real data (no interior EMPTY gaps among the 16 survivors).
    for (int d = 0; d < HISTORY_COLUMNS - 1; ++d) {
      CHECK(r.cols[d].barLevel != HISTORY_EMPTY);
    }
  }

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
