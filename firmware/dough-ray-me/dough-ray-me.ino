// dough-ray-me -- fermentation temperature controller (tickets 1-3, 5: control
// spine + safety gate + keypad UI + Stats screen numbers).
//
// Holds the Fermenting Box at a baker-chosen Setpoint using a hysteresis control
// law, on a non-blocking loop (ADR-0002): the DS18B20 is read asynchronously and
// there is no delay() in loop(), so the keypad never feels laggy. Four pure,
// host-tested units carry the logic -- decideHeat() in control.h (the control
// law), safetyGate() in safety.h (the fail-safe gate applied on top of it), the
// UI state machine in ui.h, and the Stats accumulators in stats.h; this file is
// the thin hardware shell around them.
//
// The LCD Keypad Shield adds live editing: Left/Right page four screens
// (Home / Setpoint / Tolerance / Stats), Up/Down edit the current screen's value
// (and the Setpoint straight from Home), and Select returns to Home. Edits are
// live with no confirm -- the new Setpoint/Tolerance feed decideHeat() at once.
// On a Sensor Fault or the 35 C Safety Cutoff the gate forces the heater OFF and
// a distinct Alarm screen overrides the UI (ADR-0001).
//
// Later tickets add: EEPROM persistence (#4) and the boot splash + full serial
// line (#6).
//
// Pin map (from the thermostat PoC, unchanged):
//   D2        DS18B20 data (4.7k pull-up to 5V)
//   D3        relay IN (active-HIGH) -> bulb
//   D4-D7     LCD data
//   D8, D9    LCD RS, E
//   D10       LCD backlight (shield, default-on)
//   D13       LED_BUILTIN -- mirrors the relay
//   A0        keypad analog ladder (5 buttons on one pin)

#include <LiquidCrystal.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include "control.h"
#include "safety.h"
#include "ui.h"
#include "stats.h"

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
const uint8_t KEYPAD_PIN    = A0;      // all 5 buttons on one analog resistor ladder

// --- Timing -----------------------------------------------------------------
const unsigned long SAMPLE_INTERVAL_MS = 1000;  // one reading per second
const unsigned long CONVERSION_MS      = 750;   // DS18B20 12-bit conversion time
const unsigned long BUTTON_SCAN_MS     = 5;     // poll the keypad every few ms
const unsigned long REPEAT_DELAY_MS    = 500;   // hold this long before auto-repeat starts
const unsigned long REPEAT_RATE_MS     = 150;   // then emit a repeat this often

// --- Keypad analog ladder ---------------------------------------------------
// DFRobot LCD Keypad Shield thresholds. Each button ties A0 to a different
// voltage via a resistor divider; we bucket the raw 0-1023 reading. "None"
// (all buttons up) floats near 1023.
const int KEY_ADC_RIGHT  = 50;
const int KEY_ADC_UP     = 195;
const int KEY_ADC_DOWN   = 380;
const int KEY_ADC_LEFT   = 555;
const int KEY_ADC_SELECT = 790;

// --- Hardware ---------------------------------------------------------------
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// --- Runtime state ----------------------------------------------------------
bool heating = false;                 // current heater state, held across loops
bool overTempLatched = false;         // Safety Cutoff latch, threaded to safety.h
SafetyAlarm currentAlarm = ALARM_NONE;    // latest safety verdict, drives the Alarm screen
bool conversionPending = false;       // waiting on an async DS18B20 conversion
unsigned long lastRequestMs = 0;      // when the current sample was requested
float lastTempC = DEVICE_DISCONNECTED_C;  // most recent valid reading, for repaints on UI change

UiState ui = uiInitial();             // live screen + Setpoint + Tolerance (pure unit)

// Since-power-on Stats shown on the Stats screen (min/max Box Air Temperature +
// Heater Duty). The shield's physical RESET reboots the Uno, so this re-inits to
// statsInitial() for a fresh observation while the Setpoint/Tolerance persist.
StatsState stats = statsInitial();
unsigned long lastAccrueMs = 0;       // for accruing Heater Duty from elapsed time

// Keypad edge detection + auto-repeat (impure timing lives here, not in ui.h).
UiButton      heldButton   = UI_BTN_NONE;  // button currently pressed, or NONE
unsigned long lastScanMs   = 0;            // last time we sampled A0
unsigned long nextRepeatMs = 0;            // when the held button next auto-repeats

// LCD repaint tracking (repaint only on change, to avoid flicker).
UiScreen shownScreen        = UI_SCREEN_COUNT;  // sentinel: force first paint
int      shownTempDeci      = INT16_MIN;        // last shown temp * 10
int      shownHeating       = -1;               // last shown heater state
int      shownSetpointDeci  = INT16_MIN;        // last shown Setpoint * 10
int      shownToleranceCenti = INT16_MIN;        // last shown Tolerance * 100
int      shownAlarm         = -1;               // last shown SafetyAlarm, or sentinel
int      shownMinDeci       = INT16_MIN;        // last shown min Box Air Temp * 10
int      shownMaxDeci       = INT16_MIN;        // last shown max Box Air Temp * 10
int      shownDutyPct       = -1;               // last shown Heater Duty percent

// Round a temperature to fixed-point deci/centi for cheap change detection.
int toDeci(float v)  { return (int)(v * 10.0  + (v >= 0 ? 0.5 : -0.5)); }
int toCenti(float v) { return (int)(v * 100.0 + (v >= 0 ? 0.5 : -0.5)); }

void relayApply(bool on) {
  digitalWrite(RELAY_PIN, on == RELAY_ACTIVE_HIGH ? HIGH : LOW);
  digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
}

// The single source of truth for Alarm wording, shared by the LCD Alarm screen
// and the serial line so the two can't drift apart. ALARM_NONE has no label --
// the LCD shows no Alarm and the serial line prints "none" itself.
const char* alarmLabel(SafetyAlarm a) {
  switch (a) {
    case ALARM_SENSOR_FAULT: return "Sensor fault";
    case ALARM_OVER_TEMP:    return "Over-temp >35C";
    default:                 return "";
  }
}

// Decode the raw A0 reading into a button, or NONE when all are up.
UiButton readKeypad() {
  int adc = analogRead(KEYPAD_PIN);
  if (adc < KEY_ADC_RIGHT)  return UI_BTN_RIGHT;
  if (adc < KEY_ADC_UP)     return UI_BTN_UP;
  if (adc < KEY_ADC_DOWN)   return UI_BTN_DOWN;
  if (adc < KEY_ADC_LEFT)   return UI_BTN_LEFT;
  if (adc < KEY_ADC_SELECT) return UI_BTN_SELECT;
  return UI_BTN_NONE;
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
  // First updateDisplay() paints the full Home screen (sentinels force it).
}

// Force a full repaint of the value fields -- used when the active screen or the
// Alarm state changes, since either shows different fields in the same cells.
void invalidateDisplay() {
  shownTempDeci      = INT16_MIN;
  shownHeating       = -1;
  shownSetpointDeci  = INT16_MIN;
  shownToleranceCenti = INT16_MIN;
  shownMinDeci       = INT16_MIN;
  shownMaxDeci       = INT16_MIN;
  shownDutyPct       = -1;
}

// Repaint only the parts of the LCD that changed. An active Alarm overrides every
// screen with a distinct Alarm display, so a force-OFF box is never mistaken for
// the normal Home screen (ADR-0001); otherwise the current UI screen is shown.
void updateDisplay() {
  // Entering or leaving an Alarm repaints the whole LCD. Reset the field
  // trackers and force a screen relabel so the normal screen repaints cleanly
  // once the Alarm clears.
  if ((int)currentAlarm != shownAlarm) {
    shownAlarm = (int)currentAlarm;
    invalidateDisplay();
    shownScreen = UI_SCREEN_COUNT;
    lcd.clear();
    if (currentAlarm != ALARM_NONE) {
      lcd.setCursor(0, 0);
      lcd.print("** ALARM **");
      lcd.setCursor(0, 1);
      lcd.print(alarmLabel(currentAlarm));
    }
  }
  if (currentAlarm != ALARM_NONE) return;   // Alarm screen is static; nothing more to paint

  // Wipe and relabel both rows when the screen itself changed.
  if (ui.screen != shownScreen) {
    shownScreen = ui.screen;
    invalidateDisplay();
    lcd.clear();
    lcd.setCursor(0, 0);
    switch (ui.screen) {
      case UI_HOME:      lcd.print("dough-ray-me");  break;
      case UI_SETPOINT:  lcd.print("Setpoint");      break;
      case UI_TOLERANCE: lcd.print("Tolerance");     break;
      case UI_STATS:     lcd.print("Stats");         break;
      default: break;
    }
  }

  int tempDeci      = toDeci(lastTempC);
  int setDeci       = toDeci(ui.setpointC);
  int tolCenti      = toCenti(ui.toleranceC);
  bool haveTemp     = (lastTempC != DEVICE_DISCONNECTED_C);

  switch (ui.screen) {
    case UI_HOME:
      // Home shows all four read-only fields the spec asks for, across two rows:
      // Row 0: Box Air Temperature + heat state.  Row 1: Setpoint + Tolerance.
      if (tempDeci != shownTempDeci || (int)heating != shownHeating) {
        shownTempDeci = tempDeci;
        shownHeating  = (int)heating;
        lcd.setCursor(0, 0);
        lcd.print("T:");
        if (haveTemp) lcd.print(lastTempC, 1);
        else          lcd.print("--.-");
        lcd.print("C Heat:");
        lcd.print(heating ? "ON " : "OFF");   // both 3 chars -> row stays 16 wide
      }
      if (setDeci != shownSetpointDeci || tolCenti != shownToleranceCenti) {
        shownSetpointDeci   = setDeci;
        shownToleranceCenti = tolCenti;
        lcd.setCursor(0, 1);
        lcd.print("S:");
        lcd.print(ui.setpointC, 1);
        lcd.print(" +/-");
        lcd.print(ui.toleranceC, 2);
        lcd.print("  ");             // pad to wipe any stale trailing char
      }
      break;

    case UI_SETPOINT:
      // Row 1: the live Setpoint the baker is editing.
      if (setDeci != shownSetpointDeci) {
        shownSetpointDeci = setDeci;
        lcd.setCursor(0, 1);
        lcd.print("Set: ");
        lcd.print(ui.setpointC, 1);
        lcd.print(" C   ");
      }
      break;

    case UI_TOLERANCE:
      // Row 1: the live Tolerance (+/- half-band) the baker is editing.
      if (tolCenti != shownToleranceCenti) {
        shownToleranceCenti = tolCenti;
        lcd.setCursor(0, 1);
        lcd.print("+/-");
        lcd.print(ui.toleranceC, 2);
        lcd.print(" C   ");
      }
      break;

    case UI_STATS: {
      // Since-power-on Stats: row 0 the min/max Box Air Temperature swing, row 1
      // the Heater Duty. Both read "--.-" / 0% until the first valid reading.
      int minDeci = toDeci(stats.minTempC);
      int maxDeci = toDeci(stats.maxTempC);
      int dutyPct = statsDutyPercent(stats);
      if (minDeci != shownMinDeci || maxDeci != shownMaxDeci) {
        shownMinDeci = minDeci;
        shownMaxDeci = maxDeci;
        lcd.setCursor(0, 0);
        lcd.print("Lo:");
        if (stats.seenTemp) lcd.print(stats.minTempC, 1);
        else                lcd.print("--.-");
        lcd.print(" Hi:");
        if (stats.seenTemp) lcd.print(stats.maxTempC, 1);
        else                lcd.print("--.-");
        lcd.print(" ");          // pad to wipe any stale trailing char
      }
      if (dutyPct != shownDutyPct) {
        shownDutyPct = dutyPct;
        lcd.setCursor(0, 1);
        lcd.print("Heater Duty ");
        lcd.print(dutyPct);
        lcd.print("%   ");       // pad to wipe a shrinking number (100 -> 42 -> 5)
      }
      break;
    }

    default: break;
  }
}

// Fold a fresh reading through the control decision and the safety gate, then
// drive the outputs. The live Setpoint/Tolerance from the UI state machine feed
// the control law; the safety gate (safety.h) sits on top and can only ever
// force the heater further OFF -- never ON (ADR-0001).
void handleReading(float tempC) {
  bool sensorOk = (tempC >= SENSOR_MIN_C) && (tempC <= SENSOR_MAX_C);
  if (sensorOk) {
    lastTempC = tempC;               // hold the last known-good temp for the Home screen
    stats = statsObserveTemp(stats, tempC);   // fold into the min/max Stats swing
  }

  // What the control law wants -- only meaningful on a trustworthy reading; the
  // gate ignores it anyway when the sensor has faulted.
  bool controlHeat = sensorOk ? decideHeat(tempC, ui.setpointC, ui.toleranceC, heating)
                              : false;

  SafetyDecision safe = safetyGate(tempC, sensorOk, controlHeat, overTempLatched);
  heating = safe.heatOn;
  overTempLatched = safe.overTempLatched;
  currentAlarm = safe.alarm;
  relayApply(heating);

  Serial.print("T:");
  if (sensorOk) Serial.print(tempC, 1);
  else          Serial.print("--.-");
  Serial.print(" C  Set:");
  Serial.print(ui.setpointC, 1);
  Serial.print("  Tol:");
  Serial.print(ui.toleranceC, 2);
  Serial.print("  Heat:");
  Serial.print(heating ? "ON" : "OFF");
  Serial.print("  Alarm:");
  Serial.println(safe.alarm == ALARM_NONE ? "none" : alarmLabel(safe.alarm));

  updateDisplay();
}

// Non-blocking keypad scan: edge-detect the analog ladder and auto-repeat on
// hold. Emits at most one pure UiButton event per call; the pure state machine
// (ui.h) does the rest. Auto-repeat timing is impure and stays here (ADR-0002:
// no delay(), millis()-scheduled).
void scanButtons(unsigned long now) {
  if ((now - lastScanMs) < BUTTON_SCAN_MS) return;
  lastScanMs = now;

  UiButton btn = readKeypad();
  UiButton event = UI_BTN_NONE;

  if (btn != heldButton) {
    // Edge: a new button went down (or all released). A press fires one step
    // immediately, then arms the initial pre-repeat pause.
    heldButton = btn;
    if (btn != UI_BTN_NONE) {
      event = btn;
      nextRepeatMs = now + REPEAT_DELAY_MS;
    }
  } else if (btn != UI_BTN_NONE && (long)(now - nextRepeatMs) >= 0) {
    // Still held past the pause: auto-repeat at the steady rate.
    event = btn;
    nextRepeatMs = now + REPEAT_RATE_MS;
  }

  if (event != UI_BTN_NONE) {
    ui = uiStep(ui, event);   // live edit: new Setpoint/Tolerance used next reading
    updateDisplay();          // reflect navigation / value change at once
  }
}

void loop() {
  unsigned long now = millis();

  // Accrue Heater Duty from the interval since the last pass, against the actual
  // relay state (heating) -- so a Safety Cutoff or Sensor Fault that forces the
  // heater OFF is counted as OFF. Done every loop (ADR-0002: elapsed time, never
  // a blocking counter); heating only changes in handleReading below, so the
  // interval just gone carried whatever state we accrue here.
  stats = statsAccrue(stats, heating, now - lastAccrueMs);
  lastAccrueMs = now;

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

  // Scan the keypad every few ms so presses are never swallowed by the sensor.
  scanButtons(now);
}
