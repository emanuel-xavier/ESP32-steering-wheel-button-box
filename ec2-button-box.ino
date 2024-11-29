#include <BleConnectionStatus.h>
#include <BleGamepad.h>
#include <BleGamepadConfiguration.h>

const int pins[] = {32, 33, 25, 26, 27, 14, 13, 4, 16, 17, 18, 19, 21, 22};
const int numberOfPins = sizeof(pins) / sizeof(pins[0]);
int buttonPreviousState[numberOfPins];

BleGamepad bleGamepad;

void setup() {
  Serial.begin(9600);

  for(int i = 0; i < numberOfPins; i++) {
    pinMode(pins[i], INPUT_PULLUP); 
    buttonPreviousState[i] = LOW;
  }

  bleGamepad.begin();
  Serial.println("Setup done");
}

void loop() {
  if (bleGamepad.isConnected()) {
    for(int i = 0; i < numberOfPins; i++) {
      int btnCurrentState = digitalRead(pins[i]);

      if (btnCurrentState != buttonPreviousState[i]) {
        if (btnCurrentState == LOW) {
          Serial.println("button pressed");
           bleGamepad.press(BUTTON_1 + i);
        } else {
          Serial.println("button released");
          bleGamepad.release(BUTTON_1 + i);
        }
        buttonPreviousState[i] = btnCurrentState;
      }
    }
  } else {
    Serial.println("bleGamepad is not connected");
  }
  delay(10);
}
