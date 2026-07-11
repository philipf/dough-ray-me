// Host tests for the pure persistence unit. Dependency-free: a tiny CHECK macro,
// compiled and run natively (see run.sh). Expected behaviour comes from SPEC.md
// (persist to EEPROM, debounced ~2 s; boot to the documented defaults on an
// uninitialised/invalid EEPROM), not from re-deriving the implementation. The
// real EEPROM.get/put and millis() are impure glue in the .ino; only the
// debounce state machine and the validity check are exercised here.
#include <cmath>
#include <cstdio>
#include "../dough-ray-me/persist.h"

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

// Setpoint/Tolerance land on the exact 0.5 / 0.25 grid, all representable in
// binary float, so equality is safe -- and equality is exactly what the debounce
// change detection uses, so the tests mirror the code under test.
static bool near(float a, float b) {
  float d = a - b;
  return (d < 0 ? -d : d) < 1e-4f;
}

int main() {
  // ==== persistDecode: what to trust on boot =================================
  // The defaults the spec documents for an uninitialised or invalid EEPROM.

  // --- A valid, in-range block is trusted as-is -----------------------------
  {
    PersistValues v = persistDecode(PERSIST_MAGIC, 26.5f, 1.25f);
    CHECK(near(v.setpointC, 26.5f));
    CHECK(near(v.toleranceC, 1.25f));
  }
  // Both range extremes are valid (guards against an off-by-one clamp).
  {
    PersistValues lo = persistDecode(PERSIST_MAGIC, 18.0f, 0.25f);
    CHECK(near(lo.setpointC, 18.0f) && near(lo.toleranceC, 0.25f));
    PersistValues hi = persistDecode(PERSIST_MAGIC, 30.0f, 2.0f);
    CHECK(near(hi.setpointC, 30.0f) && near(hi.toleranceC, 2.0f));
  }

  // --- Wrong magic (first-ever flash, 0xFF bytes) -> documented defaults ------
  {
    PersistValues v = persistDecode(0xFF, 26.5f, 1.25f);
    CHECK(near(v.setpointC, 24.0f));    // Setpoint boots to 24 C
    CHECK(near(v.toleranceC, 0.5f));    // Tolerance boots to +/-0.5 C
  }
  {
    PersistValues v = persistDecode(0x00, 26.5f, 1.25f);
    CHECK(near(v.setpointC, 24.0f) && near(v.toleranceC, 0.5f));
  }

  // --- Right magic but out-of-range values -> defaults, not garbage ----------
  // Either field out of range (past either end) rejects the whole block.
  {
    CHECK(near(persistDecode(PERSIST_MAGIC, 30.5f, 1.0f).setpointC, 24.0f));   // Setpoint too high
    CHECK(near(persistDecode(PERSIST_MAGIC, 17.5f, 1.0f).setpointC, 24.0f));   // Setpoint too low
    CHECK(near(persistDecode(PERSIST_MAGIC, 24.0f, 2.25f).toleranceC, 0.5f));  // Tolerance too high
    CHECK(near(persistDecode(PERSIST_MAGIC, 24.0f, 0.1f).toleranceC, 0.5f));   // Tolerance too low
  }
  // A rejected block falls back on *both* fields together.
  {
    PersistValues v = persistDecode(PERSIST_MAGIC, 99.0f, 9.0f);
    CHECK(near(v.setpointC, 24.0f) && near(v.toleranceC, 0.5f));
  }

  // --- NaN (garbage float) fails every comparison -> defaults ----------------
  {
    float nan = std::nanf("");
    PersistValues v = persistDecode(PERSIST_MAGIC, nan, nan);
    CHECK(near(v.setpointC, 24.0f) && near(v.toleranceC, 0.5f));
  }

  // --- Right magic, in range, but OFF the editable grid -> defaults ----------
  // uiStep() can only ever produce on-grid values, so an in-range off-grid
  // read-back (stale bytes from a prior firmware with different steps, or a
  // corrupt float that happens to land in range) is untrustworthy and never
  // self-corrects -- uiStep only adds/subtracts a step, so 24.3 -> 24.8 -> ...
  // Reject the whole block to the documented defaults rather than drive the
  // control law off-grid.
  {
    CHECK(near(persistDecode(PERSIST_MAGIC, 24.3f, 1.0f).setpointC,  24.0f));  // Setpoint off 0.5 grid
    CHECK(near(persistDecode(PERSIST_MAGIC, 24.0f, 0.6f).toleranceC, 0.5f));   // Tolerance off 0.25 grid
    // A rejected block falls back on *both* fields together.
    PersistValues v = persistDecode(PERSIST_MAGIC, 24.3f, 1.0f);
    CHECK(near(v.setpointC, 24.0f) && near(v.toleranceC, 0.5f));
  }
  // On-grid interior values (not just the range edges above) stay trusted, so
  // the grid check doesn't over-reject legitimately saved settings.
  {
    PersistValues v = persistDecode(PERSIST_MAGIC, 19.5f, 0.75f);
    CHECK(near(v.setpointC, 19.5f) && near(v.toleranceC, 0.75f));
  }

  // ==== persistStep: debounced writes (~2 s) =================================

  // --- Steady state: no change, no write -------------------------------------
  {
    PersistState p = persistInitial(24.0f, 0.5f);
    PersistUpdate u = persistStep(p, 24.0f, 0.5f, 0);
    CHECK(!u.write);
    CHECK(!u.state.pending);
    u = persistStep(u.state, 24.0f, 0.5f, 100000);   // long idle, still nothing
    CHECK(!u.write);
  }

  // --- A single change writes once, only after ~2 s of stability -------------
  {
    PersistState p = persistInitial(24.0f, 0.5f);
    // t=0: baker nudges the Setpoint. Timer arms; no write yet.
    PersistUpdate u = persistStep(p, 24.5f, 0.5f, 0);
    CHECK(!u.write);
    CHECK(u.state.pending);
    // t=1999: still within the window -> no write.
    u = persistStep(u.state, 24.5f, 0.5f, 1999);
    CHECK(!u.write);
    CHECK(near(u.state.committedSetpointC, 24.0f));   // EEPROM untouched so far
    // t=2000: the window closes exactly -> the one write, committing the value.
    u = persistStep(u.state, 24.5f, 0.5f, 2000);
    CHECK(u.write);
    CHECK(near(u.state.committedSetpointC, 24.5f));
    CHECK(!u.state.pending);
    // t=2001+: no further writes while the value stays put.
    u = persistStep(u.state, 24.5f, 0.5f, 2001);
    CHECK(!u.write);
  }

  // --- Ramp via hold: many steps, a single write -----------------------------
  // Each Up step restarts the debounce window, so the write fires once after the
  // ramp settles -- not once per step (protects EEPROM wear).
  {
    PersistState p = persistInitial(24.0f, 0.5f);
    PersistUpdate u = persistStep(p, 24.5f, 0.5f, 0);      // step 1, due 2000
    CHECK(!u.write);
    u = persistStep(u.state, 25.0f, 0.5f, 150);            // step 2, due 2150
    CHECK(!u.write);
    u = persistStep(u.state, 25.5f, 0.5f, 300);            // step 3, due 2300
    CHECK(!u.write);
    // t=2100: past the first step's original 2000 deadline, but the ramp pushed
    // it out -- still no write, and EEPROM still holds the old value.
    u = persistStep(u.state, 25.5f, 0.5f, 2100);
    CHECK(!u.write);
    CHECK(near(u.state.committedSetpointC, 24.0f));
    // t=2300: window from the last step closes -> exactly one write.
    u = persistStep(u.state, 25.5f, 0.5f, 2300);
    CHECK(u.write);
    CHECK(near(u.state.committedSetpointC, 25.5f));
    // No repeat write afterwards.
    u = persistStep(u.state, 25.5f, 0.5f, 2500);
    CHECK(!u.write);
  }

  // --- Change then change back cancels the pending write ----------------------
  // Nudged and undone within the window: EEPROM already holds this value, so no
  // write should ever fire.
  {
    PersistState p = persistInitial(24.0f, 0.5f);
    PersistUpdate u = persistStep(p, 24.5f, 0.5f, 0);
    CHECK(u.state.pending);
    u = persistStep(u.state, 24.0f, 0.5f, 100);   // back to committed
    CHECK(!u.write);
    CHECK(!u.state.pending);
    u = persistStep(u.state, 24.0f, 0.5f, 5000);  // long after -> still no write
    CHECK(!u.write);
  }

  // --- Tolerance is persisted the same way as the Setpoint --------------------
  {
    PersistState p = persistInitial(24.0f, 0.5f);
    PersistUpdate u = persistStep(p, 24.0f, 0.75f, 0);
    CHECK(!u.write);
    u = persistStep(u.state, 24.0f, 0.75f, 2000);
    CHECK(u.write);
    CHECK(near(u.state.committedToleranceC, 0.75f));
    CHECK(near(u.state.committedSetpointC, 24.0f));   // Setpoint left untouched
  }

  // --- Both values changed within one window -> a single combined write -------
  {
    PersistState p = persistInitial(24.0f, 0.5f);
    PersistUpdate u = persistStep(p, 24.5f, 0.5f, 0);       // Setpoint, due 2000
    CHECK(!u.write);
    u = persistStep(u.state, 24.5f, 0.75f, 500);            // Tolerance too, due 2500
    CHECK(!u.write);
    u = persistStep(u.state, 24.5f, 0.75f, 2000);           // old deadline: not due
    CHECK(!u.write);
    u = persistStep(u.state, 24.5f, 0.75f, 2500);           // new deadline: one write
    CHECK(u.write);
    CHECK(near(u.state.committedSetpointC, 24.5f));
    CHECK(near(u.state.committedToleranceC, 0.75f));
  }

  // --- millis() wrap safety (ADR-0002): the deadline may straddle rollover ----
  {
    unsigned long late = (unsigned long)-1000;   // 1000 ms before the 32-bit wrap
    PersistState p = persistInitial(24.0f, 0.5f);
    PersistUpdate u = persistStep(p, 24.5f, 0.5f, late);    // due = late+2000 = 1000 (wrapped)
    CHECK(!u.write);
    u = persistStep(u.state, 24.5f, 0.5f, 500);             // after wrap, before due
    CHECK(!u.write);
    u = persistStep(u.state, 24.5f, 0.5f, 1000);            // at the wrapped deadline
    CHECK(u.write);
    CHECK(near(u.state.committedSetpointC, 24.5f));
  }

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
