// Toggle an Omron G3MB-202P solid-state relay module every 2 seconds.
//
// Wiring (module version, blue PCB):
//   DC+ -> 5V, DC- -> GND, IN/CH -> D3
//
// AC-only relay: put the two screw terminals in series with one leg of an
// AC load. Safe to test with no load connected — watch the onboard LED.
//
// Most G3MB modules are active-HIGH. If yours switches the opposite way,
// set RELAY_ACTIVE_HIGH to false.

const uint8_t RELAY_PIN = 3;
const bool RELAY_ACTIVE_HIGH = true;

void relayOn()  { digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? HIGH : LOW); }
void relayOff() { digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? LOW : HIGH); }

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);   // diagnostic mirror of relay state
  relayOff();
  digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
  // Relay ON for 10 seconds, then OFF for 2 seconds, repeating.
  relayOn();
  digitalWrite(LED_BUILTIN, HIGH);
  delay(10000);
  relayOff();
  digitalWrite(LED_BUILTIN, LOW);
  delay(2000);
}
