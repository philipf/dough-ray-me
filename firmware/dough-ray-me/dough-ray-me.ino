// dough-ray-me -- fermentation temperature controller (ticket 1: control spine).
//
// Holds the Fermenting Box at a fixed, compile-time Setpoint using a hysteresis
// control law, on a non-blocking loop (ADR-0002): the DS18B20 is read
// asynchronously and there is no delay() in loop(), so the keypad added in a
// later ticket will never feel laggy. The decision logic lives in control.h as a
// pure, host-tested unit; this file is the thin hardware shell around it.
//
// Later tickets add: the safety gate + Alarm (#2), keypad editing of Setpoint and
// Tolerance (#3), EEPROM persistence (#4), the Stats screen (#5), and the boot
// splash + full serial line (#6).
//
// Pin map (from the thermostat PoC, unchanged):
//   D2        DS18B20 data (4.7k pull-up to 5V)
//   D3        relay IN (active-HIGH) -> bulb
//   D4-D7     LCD data
//   D8, D9    LCD RS, E
//   D10       LCD backlight (shield, default-on)
//   D13       LED_BUILTIN -- mirrors the relay
//   A0        keypad analog ladder (unused until ticket 3)

#include <LiquidCrystal.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include "control.h"

// --- Fixed tuning (becomes keypad-editable in ticket 3) ---------------------
const float SETPOINT_C  = 24.0;   // target Box Air Temperature
const float TOLERANCE_C = 0.5;    // +/- half-band around the Setpoint

// --- Pins -------------------------------------------------------------------
const uint8_t RELAY_PIN     = 3;
const bool    RELAY_ACTIVE_HIGH = true;
const int     ONE_WIRE_BUS  = 2;

// --- Timing -----------------------------------------------------------------
const unsigned long SAMPLE_INTERVAL_MS = 1000;  // one reading per second
const unsigned long CONVERSION_MS      = 750;   // DS18B20 12-bit conversion time

// --- Hardware ---------------------------------------------------------------
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// --- Runtime state ----------------------------------------------------------
bool heating = false;                 // current heater state, held across loops
bool conversionPending = false;       // waiting on an async DS18B20 conversion
unsigned long lastRequestMs = 0;      // when the current sample was requested
bool haveReading = false;             // seen at least one valid reading yet?

// LCD repaint tracking (repaint only on change, to avoid flicker).
int  shownTempDeci = INT16_MIN;       // last shown temp * 10, or sentinel
int  shownHeating  = -1;              // last shown heater state, or sentinel

void relayApply(bool on) {
  digitalWrite(RELAY_PIN, on == RELAY_ACTIVE_HIGH ? HIGH : LOW);
  digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
}

void setup() {
  Serial.begin(9600);
  sensors.begin();
  sensors.setWaitForConversion(false);  // async: requestTemperatures() returns at once

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  heating = false;
  relayApply(false);                    // heater OFF until the first valid reading

  lcd.begin(16, 2);
  lcd.print("dough-ray-me");
  lcd.setCursor(0, 1);
  lcd.print("Set:");
  lcd.print(SETPOINT_C, 1);
  lcd.print("C");
}

// Repaint only the parts of the LCD that changed.
void updateDisplay(float tempC) {
  int tempDeci = (int)(tempC * 10.0 + (tempC >= 0 ? 0.5 : -0.5));
  if (tempDeci != shownTempDeci) {
    shownTempDeci = tempDeci;
    lcd.setCursor(0, 0);
    lcd.print("Temp:");
    lcd.print(tempC, 1);
    lcd.print("C    ");           // trailing spaces wipe stale digits
  }
  if ((int)heating != shownHeating) {
    shownHeating = (int)heating;
    lcd.setCursor(0, 1);
    lcd.print("Heat:");
    lcd.print(heating ? "ON " : "OFF");
    lcd.print(" Set:");
    lcd.print(SETPOINT_C, 1);     // fixed for now; ticket 3 makes it live
  }
}

// Fold a fresh reading into the control decision and outputs.
void handleReading(float tempC) {
  if (tempC == DEVICE_DISCONNECTED_C) {
    // Basic safety: never heat on a bad reading. The full safety gate + Alarm
    // (35 C cutoff, sensor-fault UI) is ticket #2.
    heating = false;
    relayApply(false);
    Serial.println("Error: DS18B20 not found");
    lcd.setCursor(0, 0);
    lcd.print("Sensor error    ");
    shownTempDeci = INT16_MIN;    // force a repaint once a reading returns
    return;
  }

  haveReading = true;
  heating = decideHeat(tempC, SETPOINT_C, TOLERANCE_C, heating);
  relayApply(heating);

  Serial.print("T:");
  Serial.print(tempC, 1);
  Serial.print(" C  Set:");
  Serial.print(SETPOINT_C, 1);
  Serial.print("  Heat:");
  Serial.println(heating ? "ON" : "OFF");

  updateDisplay(tempC);
}

void loop() {
  unsigned long now = millis();

  // Kick off a new conversion once per interval.
  if (!conversionPending && (now - lastRequestMs) >= SAMPLE_INTERVAL_MS) {
    sensors.requestTemperatures();     // returns immediately (async mode)
    lastRequestMs = now;
    conversionPending = true;
  }

  // Collect the result once the conversion has had time to finish.
  if (conversionPending && (now - lastRequestMs) >= CONVERSION_MS) {
    conversionPending = false;
    handleReading(sensors.getTempCByIndex(0));
  }

  // Buttons will be scanned here without blocking (ticket #3).
}
