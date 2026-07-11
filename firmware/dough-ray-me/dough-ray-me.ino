// dough-ray-me -- fermentation temperature controller (tickets 1-2: control
// spine + safety gate).
//
// Holds the Fermenting Box at a fixed, compile-time Setpoint using a hysteresis
// control law, on a non-blocking loop (ADR-0002): the DS18B20 is read
// asynchronously and there is no delay() in loop(), so the keypad added in a
// later ticket will never feel laggy. The decision logic lives in two pure,
// host-tested units -- control.h (the control law) and safety.h (the fail-safe
// gate applied on top of it) -- and this file is the thin hardware shell around
// them.
//
// Later tickets add: keypad editing of Setpoint and Tolerance (#3), EEPROM
// persistence (#4), the Stats screen (#5), and the boot splash + full serial
// line (#6).
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
#include "safety.h"

// --- Fixed tuning (becomes keypad-editable in ticket 3) ---------------------
const float SETPOINT_C  = 24.0;   // target Box Air Temperature
const float TOLERANCE_C = 0.5;    // +/- half-band around the Setpoint

// A reading outside this plausible band (or DEVICE_DISCONNECTED_C, which is
// -127 and so falls below the floor) is treated as a Sensor Fault: the Box Air
// Temperature is unknown, so the safety gate forces the heater OFF. The window
// is generous but sits below the DS18B20's 85 C power-on glitch value.
const float SENSOR_MIN_C = -20.0;
const float SENSOR_MAX_C = 60.0;

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
bool overTempLatched = false;         // Safety Cutoff latch, threaded to safety.h
bool conversionPending = false;       // waiting on an async DS18B20 conversion
unsigned long lastRequestMs = 0;      // when the current sample was requested

// LCD repaint tracking (repaint only on change, to avoid flicker).
int  shownTempDeci = INT16_MIN;       // last shown temp * 10, or sentinel
int  shownHeating  = -1;              // last shown heater state, or sentinel
int  shownAlarm    = -1;              // last shown SafetyAlarm, or sentinel

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

// Repaint only the parts of the LCD that changed. On an Alarm the whole screen
// is replaced with a distinct Alarm display so a force-OFF box is never mistaken
// for the normal Home screen (ADR-0001).
void updateDisplay(float tempC, SafetyAlarm alarm) {
  if ((int)alarm != shownAlarm) {
    // Alarm state changed: repaint the whole screen. Reset the per-field
    // trackers so the next normal paint (on clearing the Alarm) isn't skipped.
    shownAlarm = (int)alarm;
    shownTempDeci = INT16_MIN;
    shownHeating  = -1;
    lcd.clear();
    if (alarm != ALARM_NONE) {
      lcd.setCursor(0, 0);
      lcd.print("** ALARM **");
      lcd.setCursor(0, 1);
      lcd.print(alarm == ALARM_SENSOR_FAULT ? "Sensor fault" : "Over-temp >35C");
    }
  }
  if (alarm != ALARM_NONE) return;   // Alarm screen is static; nothing to repaint

  // Normal Home: temperature on the top row, heat state + Setpoint below.
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
    lcd.print(" S:");             // abbreviated so the row fits the 16 columns
    lcd.print(SETPOINT_C, 1);     // fixed for now; ticket 3 makes it live
    lcd.print(" ");               // pad to 16 to wipe any stale trailing char
  }
}

// Fold a fresh reading through the control decision and the safety gate, then
// drive the outputs. The safety gate (safety.h) sits on top of the control law
// and can only ever force the heater further OFF -- never ON (ADR-0001).
void handleReading(float tempC) {
  bool sensorOk = (tempC >= SENSOR_MIN_C) && (tempC <= SENSOR_MAX_C);

  // What the control law wants -- only meaningful on a trustworthy reading; the
  // gate ignores it anyway when the sensor has faulted.
  bool controlHeat = sensorOk ? decideHeat(tempC, SETPOINT_C, TOLERANCE_C, heating)
                              : false;

  SafetyDecision safe = safetyGate(tempC, sensorOk, controlHeat, overTempLatched);
  heating = safe.heatOn;
  overTempLatched = safe.overTempLatched;
  relayApply(heating);

  Serial.print("T:");
  if (sensorOk) Serial.print(tempC, 1);
  else          Serial.print("--.-");
  Serial.print(" C  Set:");
  Serial.print(SETPOINT_C, 1);
  Serial.print("  Heat:");
  Serial.print(heating ? "ON" : "OFF");
  Serial.print("  Alarm:");
  Serial.println(safe.alarm == ALARM_NONE ? "none"
               : safe.alarm == ALARM_SENSOR_FAULT ? "SENSOR" : "OVER-TEMP");

  updateDisplay(tempC, safe.alarm);
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
