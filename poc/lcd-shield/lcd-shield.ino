// "Hello, World!" on a DFRobot LCD Keypad Shield v1.0.
//
// The shield carries a 1602 (HD44780) LCD wired in 4-bit mode to fixed
// Arduino pins. No wiring needed — just plug the shield onto the UNO.
//
//   RS -> D8    E  -> D9
//   D4 -> D4    D5 -> D5    D6 -> D6    D7 -> D7
//   backlight -> D10        buttons   -> A0 (analog ladder)

#include <LiquidCrystal.h>

LiquidCrystal lcd(8, 9, 4, 5, 6, 7);

void setup() {
  lcd.begin(16, 2);
  lcd.print("Hello, World!");
}

void loop() {
  // Message stays on screen.
}
