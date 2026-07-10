// Blink the on-board LED every 250 ms.
// Confirms the Arduino toolchain (arduino-cli) can compile and upload.

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(250);
  digitalWrite(LED_BUILTIN, LOW);
  delay(250);
}
