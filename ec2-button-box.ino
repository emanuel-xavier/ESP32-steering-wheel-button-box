#define BOUNCE_WITH_PROMPT_DETECTION

#include <Arduino.h>
#include <Bounce2.h>    // https://github.com/thomasfredericks/Bounce2
// ESP32-BLE-Gamepad 0.5.4
// NimBLE-Arduino    1.4.1
#include <BleGamepad.h> // https://github.com/lemmingDev/ESP32-BLE-Gamepad

#define numOfButtons 14

Bounce debouncers[numOfButtons];
BleGamepad bleGamepad;
byte buttonPins[numOfButtons] = {4, 5, 13, 14, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27};
byte physicalButtons[numOfButtons];

void setup() {
    for (byte currentPinIndex = 0; currentPinIndex < numOfButtons; currentPinIndex++) {
        pinMode(buttonPins[currentPinIndex], INPUT_PULLUP);
        debouncers[currentPinIndex] = Bounce();
        debouncers[currentPinIndex].attach(buttonPins[currentPinIndex]);
        debouncers[currentPinIndex].interval(5);

        physicalButtons[currentPinIndex] = currentPinIndex + 1;
    }

    BleGamepadConfiguration bleGamepadConfig;
    bleGamepadConfig.setButtonCount(numOfButtons);
    bleGamepadConfig.setAutoReport(false);
    bleGamepadConfig.setButtonCount(numOfButtons);

    bleGamepad.begin(&bleGamepadConfig);

    Serial.begin(115200);
}

void loop() {
  if (bleGamepad.isConnected()) {
    bool sendReport = false;

    for (byte currentIndex = 0; currentIndex < numOfButtons; currentIndex++)  {
        debouncers[currentIndex].update();

        if (debouncers[currentIndex].fell())  {
            bleGamepad.press(physicalButtons[currentIndex]);
            sendReport = true;
            Serial.println("Button " + String(physicalButtons[currentIndex]) + " pushed.");
        } else if (debouncers[currentIndex].rose()) {
            bleGamepad.release(physicalButtons[currentIndex]);
            sendReport = true;
            Serial.println("Button " + String(physicalButtons[currentIndex]) + " released.");
        }
      }

      if (sendReport)  {
          bleGamepad.sendReport();
      }
    } else {
      Serial.println("blegamepad not connected");
      delay(1000); 
    }
}