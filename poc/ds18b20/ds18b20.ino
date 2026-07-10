// Read a DS18B20 temperature probe and print the temperature to serial.
// Confirms the OneWire + DallasTemperature toolchain compiles, uploads,
// and that the probe reports sane readings.
//
// Wiring (parasitic power NOT used):
//   DS18B20 red    -> 5V
//   DS18B20 black  -> GND
//   DS18B20 yellow -> D2 (data), with a 4.7k pull-up resistor between data and 5V

#include <OneWire.h>
#include <DallasTemperature.h>

const int ONE_WIRE_BUS = 2;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

void setup() {
  Serial.begin(9600);
  sensors.begin();
}

void loop() {
  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);

  if (tempC == DEVICE_DISCONNECTED_C) {
    Serial.println("Error: DS18B20 not found");
  } else {
    Serial.print(tempC);
    Serial.println(" C");
  }

  delay(1000);
}
