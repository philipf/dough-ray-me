# dough-ray-me

An Arduino thermostat that keeps sourdough dough (and its starter) at a steady
temperature so it ferments on a schedule you control — not on the whims of a cold
kitchen.

## What it does

You bake in a cold kitchen and the dough won't rise in a workable time. This project
turns an insulated polystyrene box into a warm, temperature-controlled **Fermenting
Box**. An Arduino Uno reads the air temperature inside the box with a single probe and
switches a light bulb on and off (through a relay) to add heat. When the bulb is off,
the box cools on its own — there is no active cooling.

You dial a target temperature (the **Setpoint**) on the little LCD keypad and the
controller holds the box within a chosen **Tolerance** of it. That's the whole idea:
predictable fermentation you can plan your day around.

It's a closed system. No Wi-Fi, no app, no cloud. You plug it in and it works; you
unplug it to stop. The only thing that ever leaves the box is optional logging over a
USB cable when you want to study how the box behaves.

## Key features

- **Set your temperature** — 18–30 °C, adjustable in 0.5 °C steps. Boots to a sensible
  24 °C default.
- **Adjustable Tolerance** — how far the box may drift before the heater reacts
  (±0.25 to ±2.0 °C). Tighter holds temperature better; looser cycles the bulb less.
- **Remembers your settings** — Setpoint and Tolerance survive a power cut (saved to
  EEPROM), so a blip mid-bake doesn't reset your ferment.
- **Simple screen and buttons** — a 16×2 LCD with four screens (Home, Setpoint,
  Tolerance, Stats) paged with Left/Right; Up/Down edit values live.
- **Stats** — see the min/max temperature the box has settled into and how hard the
  bulb is working (**Heater Duty**) since power-on.
- **Safe by design** — a hard 35 °C cutoff independent of your Setpoint, the heater
  forced off (with an on-screen **Alarm**) if the probe fails or reads garbage, and no
  heating at all until the first valid reading on boot.

## Hardware

- Arduino Uno
- 1602 LCD Keypad Shield (16×2 display + 5 analog buttons)
- DS18B20 temperature probe (with a 4.7 kΩ pull-up)
- Solid-state relay switching a light bulb as the heat source
- An insulated polystyrene box

## Getting started

You'll need [`arduino-cli`](https://arduino.github.io/arduino-cli/) with the
`arduino:avr` core and the three libraries below installed, plus a C++ compiler
(`g++`) for the host tests. Adjust the serial port (`/dev/ttyACM0`) to match your board.

Clone, compile, and upload to a connected Uno, then watch the serial log:

```sh
git clone https://github.com/philipf/dough-ray-me.git
cd dough-ray-me/firmware/dough-ray-me

arduino-cli compile . \
  && arduino-cli upload . \
  && tio -b 9600 -m INLCRNL -t /dev/ttyACM0
```

Run the host tests (pure logic — no board or Arduino toolchain needed):

```sh
cd firmware/test
./run.sh
```

`run.sh` compiles and runs each unit test with `g++` (C++17). The control law,
safety gate, UI state machine, persistence timer and stats accumulators are all
tested here on your laptop; the hardware glue is verified by flashing the board.

## The full spec

This README is the quick tour. The complete problem statement, user stories, control
law, pin map, safety design and testing approach live in **[SPEC.md](SPEC.md)**. The
domain vocabulary is in [CONTEXT.md](CONTEXT.md) and the cross-cutting decisions are
recorded as ADRs under [docs/adr/](docs/adr/).

## Layout

- `firmware/dough-ray-me/` — the product firmware (the Arduino sketch). The control
  law, safety gate, UI state machine, persistence and stats are pure C++ units; the
  `.ino` is a thin hardware shell around them.
- `firmware/test/` — the host tests for those pure units, run with `g++` (no board
  needed).
- `poc/` — the earlier proof-of-concept sketches (blink, LCD, relay, DS18B20,
  thermostat) kept as history.

## License

This project's own code is released under the [MIT License](LICENSE).

It builds against three third-party Arduino libraries, which it uses but does **not**
include in this repository — they are installed separately via the Arduino Library
Manager. Their licenses are unaffected by this project's MIT license, and MIT is
compatible with all of them:

| Library | Version | License |
| --- | --- | --- |
| [LiquidCrystal](https://github.com/arduino-libraries/LiquidCrystal) | 1.0.7 | LGPL-2.1 |
| [OneWire](https://github.com/PaulStoffregen/OneWire) | 2.3.8 | Permissive (MIT-style) |
| [DallasTemperature](https://github.com/milesburton/Arduino-Temperature-Control-Library) | 4.0.6 | LGPL-2.1 |

Because those libraries are not redistributed here (you install them yourself), there
is nothing to relicense — each remains under its own terms.
