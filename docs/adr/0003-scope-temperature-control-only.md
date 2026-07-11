# Scope: temperature control only

dough-ray-me does exactly one thing — hold the Fermenting Box at a chosen Box Air
Temperature — and deliberately excludes several adjacent features:

- **No fermentation timer or "ready" alert.** The scheduling problem is solved by making
  the ferment *predictable* (a stable, known temperature gives known timing); the baker
  uses an ordinary kitchen/phone timer for the countdown. A built-in timer would need a
  notion of "a bake in progress," start/stop UI, and likely a buzzer — a different product.
- **No cooling.** Heat is added by the bulb and removed only by letting the box cool
  passively. Turning the bulb off is the only "cool down" mechanism.
- **No networking.** Closed-loop system; nothing leaves the box except optional serial over
  a physically attached USB cable for tuning.

This "no" on the timer is recorded explicitly because the project's motivation is scheduling,
so a future reader will reasonably wonder why timing isn't a feature — the answer is that
temperature *stability*, not an on-device countdown, is what makes the schedule predictable.
Each excluded feature is a clean future addition if ever wanted, not a gap to design around now.
