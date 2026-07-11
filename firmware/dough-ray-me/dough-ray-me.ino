// dough-ray-me -- fermentation temperature controller (tickets 1-6 + the Graph
// screen: control spine + safety gate + keypad UI + EEPROM persistence + Stats
// screen numbers + 80-minute Graph history + boot splash and the full
// once-per-sample serial line).
//
// Holds the Fermenting Box at a baker-chosen Setpoint using a hysteresis control
// law, on a non-blocking loop (ADR-0002): the DS18B20 is read asynchronously and
// there is no delay() in loop(), so the keypad never feels laggy. Six pure,
// host-tested units carry the logic -- decideHeat() in control.h (the control
// law), safetyGate() in safety.h (the fail-safe gate applied on top of it), the
// UI state machine in ui.h, the persistence unit in persist.h (the debounced
// EEPROM write timer + boot validity check), the Stats accumulators in stats.h,
// and the Graph screen's history model in history.h; this file is the thin
// hardware shell around them.
//
// The LCD Keypad Shield adds live editing: Left/Right page five screens
// (Home / Setpoint / Tolerance / Stats / Graph), Up/Down edit the current screen's
// value (and the Setpoint straight from Home), and Select returns to Home. Edits
// are live with no confirm -- the new Setpoint/Tolerance feed decideHeat() at once.
// On a Sensor Fault or the 35 C Safety Cutoff the gate forces the heater OFF and
// a distinct Alarm screen overrides the UI (ADR-0001).
//
// The chosen Setpoint and Tolerance survive a power cut: they are read from
// EEPROM on boot (falling back to the 24 C / +/-0.5 C defaults on a fresh chip)
// and written back ~2 s after they stop changing, so holding to ramp costs one
// write, not one per step (persist.h). The Stats screen shows the min/max Box
// Air Temperature and Heater Duty since power-on, cleared by the shield's RESET
// (stats.h).
//
// On power-on a brief "dough-ray-me" splash holds for ~1.5 s before the Home
// screen, and once per sample the full state is logged over USB serial (Box Air
// Temperature / Setpoint / Tolerance / heat state / Heater Duty / Alarm) for
// multi-hour tuning on a laptop -- USB cable only, nothing else leaves the box
// (ADR-0003).
//
// Pin map (from the thermostat PoC, unchanged):
//   D2        DS18B20 data (4.7k pull-up to 5V)
//   D3        relay IN (active-HIGH) -> bulb
//   D4-D7     LCD data
//   D8, D9    LCD RS, E
//   D10       LCD backlight (shield, default-on)
//   D13       LED_BUILTIN -- mirrors the relay
//   A0        keypad analog ladder (5 buttons on one pin)

#include <EEPROM.h>
#include <LiquidCrystal.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include "control.h"
#include "safety.h"
#include "ui.h"
#include "persist.h"
#include "stats.h"
#include "history.h"

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

// --- EEPROM layout ----------------------------------------------------------
// A magic byte, then the two floats. The magic lets persistDecode() tell a
// first-ever flash (0xFF bytes) from saved settings; the values are stored raw
// (EEPROM.put) and validated against their ranges on read-back (persist.h).
const int EEPROM_ADDR_MAGIC     = 0;
const int EEPROM_ADDR_SETPOINT  = 1;   // float, 4 bytes
const int EEPROM_ADDR_TOLERANCE = 5;   // float, 4 bytes

// --- Timing -----------------------------------------------------------------
const unsigned long BOOT_SPLASH_MS     = 1500;  // "dough-ray-me" splash on power-on
const unsigned long SAMPLE_INTERVAL_MS = 1000;  // one reading per second
const unsigned long CONVERSION_MS      = 750;   // DS18B20 12-bit conversion time
const unsigned long BUTTON_SCAN_MS     = 5;     // poll the keypad every few ms
const uint8_t       BUTTON_CONFIRM_SCANS = 3;   // reading must hold this many scans before it counts (~15 ms debounce)

// --- Keypad analog ladder ---------------------------------------------------
// DFRobot LCD Keypad Shield thresholds. Each button ties A0 to a different
// voltage via a resistor divider; we bucket the raw 0-1023 reading, "None"
// (all buttons up) floats near 1023. A reading below a threshold is that button.
// Values are set to the midpoints between THIS unit's measured centres, with
// margin for contact bounce -- Down in particular bounces up toward ~385, so its
// upper edge sits well clear of Left (measured ~529):
//   Right ~12   Up ~143   Down ~329   Left ~529   Select ~750   None ~1023
const int KEY_ADC_RIGHT  = 80;
const int KEY_ADC_UP     = 235;
const int KEY_ADC_DOWN   = 455;
const int KEY_ADC_LEFT   = 640;
const int KEY_ADC_SELECT = 890;

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
PersistState persist;                 // debounced-EEPROM-write state, seeded in setup()

// Since-power-on Stats shown on the Stats screen (min/max Box Air Temperature +
// Heater Duty). The shield's physical RESET reboots the Uno, so this re-inits to
// statsInitial() for a fresh observation while the Setpoint/Tolerance persist.
StatsState stats = statsInitial();
unsigned long lastAccrueMs = 0;       // for accruing Heater Duty from elapsed time

// The Graph screen's 80-minute history (history.h): 16 windows of 5 minutes, fed
// the same elapsed-time + relay state as the Stats Heater Duty, plus each valid
// Box Air Temperature reading. RAM-only, so the shield's RESET clears it back to
// historyInitial() while the Setpoint/Tolerance persist -- the Stats lifecycle.
HistoryState history = historyInitial();

// --- Graph CGRAM glyphs -----------------------------------------------------
// The bottom-row deviation bar is a centered (diverging) bar drawn in one 5x8
// cell per column: the midline sits between rows 3 and 4, hot windows grow the
// bar up (+1..+4, rows 3..0) and cold windows grow it down (-1..-4, rows 4..7).
// That is 8 non-zero levels -- exactly the 8 CGRAM slots the HD44780 offers -- so
// the on-Setpoint level 0 is drawn with the built-in '-' instead, and EMPTY
// columns are left blank. Slot layout: 0..3 = +1..+4, 4..7 = -1..-4.
const uint8_t GRAPH_CGRAM_COUNT   = 8;
const uint8_t GRAPH_NEG_SLOT_BASE = 4;   // CGRAM slots 4..7 hold the -1..-4 bars
byte graphGlyphs[GRAPH_CGRAM_COUNT][8] = {
  {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x00},  // slot 0: +1
  {0x00, 0x00, 0x1F, 0x1F, 0x00, 0x00, 0x00, 0x00},  // slot 1: +2
  {0x00, 0x1F, 0x1F, 0x1F, 0x00, 0x00, 0x00, 0x00},  // slot 2: +3
  {0x1F, 0x1F, 0x1F, 0x1F, 0x00, 0x00, 0x00, 0x00},  // slot 3: +4
  {0x00, 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00},  // slot 4: -1
  {0x00, 0x00, 0x00, 0x00, 0x1F, 0x1F, 0x00, 0x00},  // slot 5: -2
  {0x00, 0x00, 0x00, 0x00, 0x1F, 0x1F, 0x1F, 0x00},  // slot 6: -3
  {0x00, 0x00, 0x00, 0x00, 0x1F, 0x1F, 0x1F, 0x1F},  // slot 7: -4
};

// Keypad edge detection with a debounce (click-only; the impure A0 sampling
// lives here, not in ui.h). The analog ladder emits a short burst of readings as
// a button makes/breaks contact -- some in a neighbouring button's band -- so a
// reading is only accepted once it has held for BUTTON_CONFIRM_SCANS scans, and
// each confirmed press fires exactly one event.
UiButton      heldButton      = UI_BTN_NONE;  // last confirmed button, or NONE
UiButton      candidateButton = UI_BTN_NONE;  // reading awaiting confirmation
uint8_t       candidateScans  = 0;            // consecutive scans it has held for
unsigned long lastScanMs      = 0;            // last time we sampled A0

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
HistoryRender shownGraph;                        // last-drawn Graph render model
bool     shownGraphValid    = false;            // false forces a full Graph repaint

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

// Read the saved Setpoint/Tolerance back from EEPROM and apply them to the live
// UI state, so the control law runs at the baker's chosen values from the first
// reading. persistDecode() (persist.h) supplies the 24 C / +/-0.5 C defaults
// when the EEPROM is uninitialised (a fresh chip) or holds out-of-range garbage.
void loadSettings() {
  uint8_t magic;
  float   storedSetpointC;
  float   storedToleranceC;
  EEPROM.get(EEPROM_ADDR_MAGIC, magic);
  EEPROM.get(EEPROM_ADDR_SETPOINT, storedSetpointC);
  EEPROM.get(EEPROM_ADDR_TOLERANCE, storedToleranceC);

  PersistValues v = persistDecode(magic, storedSetpointC, storedToleranceC);
  ui.setpointC  = v.setpointC;
  ui.toleranceC = v.toleranceC;
  // Seed the debounce state to what EEPROM now holds, so no write fires until the
  // baker actually changes a value.
  persist = persistInitial(v.setpointC, v.toleranceC);
}

// Commit the debounced Setpoint/Tolerance to EEPROM. EEPROM.put writes only the
// bytes that changed, sparing cells that already hold the right value.
void saveSettings() {
  EEPROM.put(EEPROM_ADDR_MAGIC, PERSIST_MAGIC);
  EEPROM.put(EEPROM_ADDR_SETPOINT, persist.committedSetpointC);
  EEPROM.put(EEPROM_ADDR_TOLERANCE, persist.committedToleranceC);
}

void setup() {
  Serial.begin(9600);
  sensors.begin();
  sensors.setWaitForConversion(false);  // async: requestTemperatures() returns at once

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  heating = false;
  relayApply(false);                    // heater OFF until the first valid reading

  loadSettings();                       // restore the saved Setpoint/Tolerance (or defaults)

  lcd.begin(16, 2);

  // Load the 8 deviation-bar glyphs into CGRAM once; the Graph screen writes them
  // by slot number. Level 0 uses the built-in '-', so all 8 slots are bars.
  for (uint8_t i = 0; i < GRAPH_CGRAM_COUNT; ++i) lcd.createChar(i, graphGlyphs[i]);

  // Boot splash: name the box for ~1.5 s, then land straight on Home. This
  // delay() is a one-time boot step, not the running path -- the control loop
  // hasn't started, the heater is already OFF, and nothing is being sampled or
  // edited yet, so it does not touch the keypad responsiveness ADR-0002 protects
  // in loop().
  lcd.setCursor(2, 0);
  lcd.print("dough-ray-me");
  delay(BOOT_SPLASH_MS);
  updateDisplay();   // paint Home now (temp reads "--.-" until the first sample)
                     // so the splash hands straight to Home with no blank gap
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
  shownGraphValid    = false;   // the whole 16x2 Graph grid repaints from scratch
}

// Translate one column's render codes (history.h) into the byte to write. The bar
// maps EMPTY -> blank, level 0 -> built-in '-', +1..+4 -> CGRAM slots 0..3 and
// -1..-4 -> slots 4..7; the duty maps EMPTY -> blank, else the ASCII digit.
uint8_t graphBarByte(int barLevel) {
  if (barLevel == HISTORY_EMPTY) return ' ';
  if (barLevel == 0)             return '-';
  if (barLevel > 0)              return (uint8_t)(barLevel - 1);            // slots 0..3
  return (uint8_t)(GRAPH_NEG_SLOT_BASE + (-barLevel - 1));                 // slots 4..7
}
uint8_t graphDutyByte(int dutyDigit) {
  if (dutyDigit == HISTORY_EMPTY)  return ' ';
  if (dutyDigit == HISTORY_CUTOFF) return '!';   // the 35 C Safety Cutoff fired here
  return (uint8_t)('0' + dutyDigit);
}

// Paint the full 16x2 Graph grid from the history render model: top row the
// per-window Heater Duty digit, bottom row the Setpoint-centered deviation bar,
// newest window on the right. Only cells that changed are rewritten (Rule 7), so
// the live right edge moves each second without flickering the frozen columns.
void paintGraph() {
  HistoryRender r = historyRender(history, ui.setpointC);
  for (int c = 0; c < HISTORY_COLUMNS; ++c) {
    if (!shownGraphValid || r.cols[c].dutyDigit != shownGraph.cols[c].dutyDigit) {
      lcd.setCursor(c, 0);
      lcd.write(graphDutyByte(r.cols[c].dutyDigit));
    }
    if (!shownGraphValid || r.cols[c].barLevel != shownGraph.cols[c].barLevel) {
      lcd.setCursor(c, 1);
      lcd.write(graphBarByte(r.cols[c].barLevel));
    }
  }
  shownGraph = r;
  shownGraphValid = true;
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

    case UI_GRAPH:
      // Headerless: both rows are the graph, no title and no numeric temperature.
      paintGraph();
      break;

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
    history = historyObserveTemp(history, tempC);  // fold into the current Graph window's mean
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
  // Heater Duty (percent ON since power-on), from the same accumulator the Stats
  // screen shows -- statsAccrue() in loop() has already folded in the interval up
  // to now, so this is the current duty.
  Serial.print("  Duty:");
  Serial.print(statsDutyPercent(stats));
  Serial.print("%  Alarm:");
  Serial.println(safe.alarm == ALARM_NONE ? "none" : alarmLabel(safe.alarm));

  updateDisplay();
}

// Non-blocking keypad scan: debounce the analog ladder, then edge-detect so each
// press fires exactly one event (click-only -- no auto-repeat). The pure state
// machine (ui.h) does the rest. Stays non-blocking (ADR-0002: no delay(),
// millis()-scheduled).
void scanButtons(unsigned long now) {
  if ((now - lastScanMs) < BUTTON_SCAN_MS) return;
  lastScanMs = now;

  UiButton btn = readKeypad();

  // Debounce: a reading must repeat across BUTTON_CONFIRM_SCANS scans before we
  // trust it. A fresh value restarts the count, so a lone contact-bounce sample
  // (which can land in the wrong band mid-press) never survives to fire.
  if (btn != candidateButton) {
    candidateButton = btn;
    candidateScans  = 1;
    return;
  }
  if (candidateScans < BUTTON_CONFIRM_SCANS) {
    candidateScans++;
    return;                        // not held long enough yet
  }

  // Confirmed stable. Edge-detect against the last confirmed button so a held key
  // fires once, not repeatedly.
  if (btn == heldButton) return;   // no edge: still held, or still released
  heldButton = btn;
  if (btn == UI_BTN_NONE) return;  // a release, not a new press

  ui = uiStep(ui, btn);   // live edit: new Setpoint/Tolerance used next reading
  updateDisplay();        // reflect navigation / value change at once
}

void loop() {
  unsigned long now = millis();

  // Accrue Heater Duty from the interval since the last pass, against the actual
  // relay state (heating) -- so a Safety Cutoff or Sensor Fault that forces the
  // heater OFF is counted as OFF. Done every loop (ADR-0002: elapsed time, never
  // a blocking counter); heating only changes in handleReading below, so the
  // interval just gone carried whatever state we accrue here. The same interval
  // feeds the Graph history, which also drives its 5-minute window rollover.
  unsigned long accrueDelta = now - lastAccrueMs;
  lastAccrueMs = now;
  stats = statsAccrue(stats, heating, accrueDelta);
  // The Graph history also latches a Safety Cutoff scar. The signal is the
  // over-temp latch, not the display Alarm: safety.h holds overTempLatched even
  // while a Sensor Fault masks the Alarm (we can't confirm the box cooled to the
  // re-arm point through a faulted probe), so the latch -- not currentAlarm -- is
  // what keeps the heater forced OFF across such an interval. It holds the value
  // from the last reading, which governed the interval just gone (as `heating`
  // does for the relay). A pure Sensor Fault leaves it false, so only the 35 C
  // over-temp excursion scars a window (issue #13).
  history = historyAccrue(history, heating, accrueDelta, overTempLatched);

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

  // Persist the live Setpoint/Tolerance ~2 s after they stop changing. The pure
  // debounce timer (persist.h) decides *when*; a held ramp keeps restarting it,
  // so a whole adjustment costs one EEPROM write, not one per step.
  PersistUpdate pu = persistStep(persist, ui.setpointC, ui.toleranceC, now);
  persist = pu.state;
  if (pu.write) saveSettings();
}
