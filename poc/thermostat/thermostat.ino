// Closed-loop thermostat: read a DS18B20, switch a relay (heater) to hold a
// setpoint with hysteresis, and show live status on the 1602 LCD Keypad Shield
// while also printing to serial. Combines the ds18b20, lcd-shield and relay
// PoCs into one non-overlapping pin map.
//
// Pin map (no overlaps):
//   D2        DS18B20 data (4.7k pull-up between data and 5V; parasitic NOT used)
//   D3        relay IN/CH (Omron G3MB-202P module, active-HIGH)
//   D4-D7     LCD data   (shield)
//   D8, D9    LCD RS, E  (shield)
//   D10       LCD backlight (shield) -- left untouched, default-on. If the
//             screen comes up dark, add: pinMode(10, OUTPUT); digitalWrite(10, HIGH);
//   D13       LED_BUILTIN -- diagnostic mirror of the relay state
//   A0        buttons, all 5 on one analog resistor ladder (shield) -- unused here
//   A1-A5, D11, D12   free
//
// NOTE: the standalone relay.ino sets RELAY_PIN = 2, which would collide with
// the DS18B20 on D2. This PoC uses D3 (matching relay.ino's own wiring comment).
//
// Control law: heater ON below (setpoint - band/2), OFF above (setpoint + band/2),
// hold state in between. The band prevents the relay chattering around the target.
//
// Known limitation: blocking delay(1000) loop; the 12-bit conversion inside
// requestTemperatures() also blocks ~750ms. Fine for a slow thermal load.

#include <LiquidCrystal.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// --- Thermostat tuning ------------------------------------------------------
const float SETPOINT_C   = 22.0;   // target temperature
const float HYSTERESIS_C = 1.0;    // total dead band around the setpoint
const float ON_BELOW_C   = SETPOINT_C - HYSTERESIS_C / 2.0;  // 21.5: turn heat ON
const float OFF_ABOVE_C  = SETPOINT_C + HYSTERESIS_C / 2.0;  // 22.5: turn heat OFF

// --- Relay ------------------------------------------------------------------
const uint8_t RELAY_PIN = 3;
const bool RELAY_ACTIVE_HIGH = true;

void relayOn()  { digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? HIGH : LOW); }
void relayOff() { digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? LOW : HIGH); }

// --- LCD + sensor -----------------------------------------------------------
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);

const int ONE_WIRE_BUS = 2;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

bool heating = false;   // current relay/heater state (held across loops)

void setup() {
  Serial.begin(9600);
  sensors.begin();

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  relayOff();
  digitalWrite(LED_BUILTIN, LOW);

  lcd.begin(16, 2);
  lcd.print("Temp: reading...");
}

// Mirror the heating flag onto the relay and the diagnostic LED.
void applyHeat(bool on) {
  heating = on;
  if (on) relayOn(); else relayOff();
  digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
}

void loop() {
  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);

  if (tempC == DEVICE_DISCONNECTED_C) {
    // Fail safe: no reading -> heater OFF.
    applyHeat(false);
    Serial.println("Error: DS18B20 not found");
    lcd.setCursor(0, 0);
    lcd.print("Sensor error    ");
    lcd.setCursor(0, 1);
    lcd.print("Heat:OFF Set:");
    lcd.print((int)SETPOINT_C);
    lcd.print("C");
    delay(1000);
    return;
  }

  // Hysteresis control: only flip near the band edges, hold in between.
  if (tempC <= ON_BELOW_C) {
    applyHeat(true);
  } else if (tempC >= OFF_ABOVE_C) {
    applyHeat(false);
  }

  // Serial: temperature + heater state.
  Serial.print(tempC);
  Serial.print(" C  Heat:");
  Serial.println(heating ? "ON" : "OFF");

  // LCD line 1: temperature. Trailing spaces wipe stale digits.
  lcd.setCursor(0, 0);
  lcd.print("Temp: ");
  lcd.print(tempC);
  lcd.print(" C   ");

  // LCD line 2: heater state + setpoint. Fixed 16-char layout.
  lcd.setCursor(0, 1);
  lcd.print("Heat:");
  lcd.print(heating ? "ON " : "OFF");
  lcd.print(" Set:");
  lcd.print((int)SETPOINT_C);
  lcd.print("C");

  delay(1000);
}
