// Show a DS18B20 temperature reading on the 1602 LCD Keypad Shield AND on
// serial. Combines the ds18b20 and lcd-shield PoCs into one sketch, chosen so
// that no two peripherals share a pin.
//
// Pin map (no overlaps):
//   D2        DS18B20 data (4.7k pull-up between data and 5V; parasitic NOT used)
//   D4-D7     LCD data   (shield)
//   D8, D9    LCD RS, E  (shield)
//   D10       LCD backlight (shield) -- left untouched, default-on. If the
//             screen comes up dark, add: pinMode(10, OUTPUT); digitalWrite(10, HIGH);
//   A0        buttons, all 5 on one analog resistor ladder (shield) -- unused here
//
// Reserved for later PoCs (deliberately not driven here):
//   D3        relay IN/CH. NOTE: relay.ino currently sets RELAY_PIN = 2, which
//             collides with the DS18B20 above. When building the relay PoC,
//             change that constant to 3 to match relay.ino's own wiring comment.
//   A1-A5, D11-D13   free
//
// Known limitation: the loop uses a blocking delay(1000), and the 12-bit
// conversion inside requestTemperatures() blocks ~750ms. Fine for a temperature
// readout; the buttons PoC will move to millis() scheduling so presses aren't
// missed.

#include <LiquidCrystal.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// LCD wired to the shield's fixed pins: RS, E, D4, D5, D6, D7.
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);

const int ONE_WIRE_BUS = 2;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

void setup() {
  Serial.begin(9600);
  sensors.begin();

  lcd.begin(16, 2);
  lcd.print("Temp: reading...");
}

void loop() {
  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);

  lcd.setCursor(0, 0);
  if (tempC == DEVICE_DISCONNECTED_C) {
    Serial.println("Error: DS18B20 not found");
    lcd.print("Sensor error    ");   // trailing spaces wipe stale digits
  } else {
    Serial.print(tempC);
    Serial.println(" C");
    lcd.print("Temp: ");
    lcd.print(tempC);
    lcd.print(" C   ");               // trailing spaces wipe stale digits
  }

  delay(1000);
}
