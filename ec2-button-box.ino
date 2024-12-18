#include <Arduino.h>
#include <Bounce2.h>  // https://github.com/thomasfredericks/Bounce2
// ESP32-BLE-Gamepad 0.5.4
// NimBLE-Arduino    1.4.1
#include <BleGamepad.h>

#define DEBOUNCE_DELAY 5
#define NUM_OF_BUTTONS 14
#define NUM_OF_ENCODERS 2

namespace Encoder {
  enum EncoderMovement { clockwise, anticlockwise, none };

  class Encoder {
    byte clk, dt;
    int lastClk;

  public:
    Encoder() {}
    Encoder(byte clk, byte dt) : clk(clk), dt(dt), lastClk(HIGH) {}

    void begin() {
      pinMode(clk, INPUT_PULLUP);
      pinMode(dt, INPUT_PULLUP);
    }

    EncoderMovement getEncoderMovement() {
      int newClk = digitalRead(clk);
      if (newClk != lastClk) {
        lastClk = newClk;
        if (newClk == LOW) {
          return (digitalRead(dt) == HIGH) ? clockwise : anticlockwise;
        }
      }
      return none;
    }
  };
}

Bounce debouncers[NUM_OF_BUTTONS];
Bounce encoderDebouncers[NUM_OF_ENCODERS * 2];
BleGamepad bleGamepad;

byte buttonPins[NUM_OF_BUTTONS] = {4, 5, 13, 14, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27};
byte encoderPins[NUM_OF_ENCODERS][2] = {{32, 33}, {2, 15}};
byte physicalButtons[NUM_OF_BUTTONS + NUM_OF_ENCODERS * 2];

Encoder::Encoder encoders[NUM_OF_ENCODERS];
byte encoderBtnStart = NUM_OF_BUTTONS;

void setupButtons() {
  for (byte i = 0; i < NUM_OF_BUTTONS; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
    debouncers[i].attach(buttonPins[i]);
    debouncers[i].interval(DEBOUNCE_DELAY);
  }
}

void setupEncoders() {
  for (byte i = 0; i < NUM_OF_ENCODERS; i++) {
    encoders[i] = Encoder::Encoder(encoderPins[i][0], encoderPins[i][1]);
    encoders[i].begin();
    encoderDebouncers[i * 2].interval(DEBOUNCE_DELAY);     // Clockwise button debounce
    encoderDebouncers[i * 2 + 1].interval(DEBOUNCE_DELAY); // Anti-clockwise button debounce
  }
}

void setupBleGamepad() {
  BleGamepadConfiguration config;
  config.setButtonCount(NUM_OF_BUTTONS + NUM_OF_ENCODERS * 2);
  config.setAutoReport(false);
  bleGamepad.begin(&config);
}

void processButtons(bool &sendReport) {
  for (byte i = 0; i < NUM_OF_BUTTONS; i++) {
    debouncers[i].update();
    if (debouncers[i].fell()) {
      bleGamepad.press(physicalButtons[i]);
      sendReport = true;
    } else if (debouncers[i].rose()) {
      bleGamepad.release(physicalButtons[i]);
      sendReport = true;
    }
  }
}

void processEncoders(bool &sendReport) {
  for (byte i = 0; i < NUM_OF_ENCODERS; i++) {
    Encoder::EncoderMovement movement = encoders[i].getEncoderMovement();
    switch (movement) {
      case Encoder::clockwise:
        encoderDebouncers[i * 2].update();
        if (encoderDebouncers[i * 2].fell()) {
          bleGamepad.press(physicalButtons[encoderBtnStart + i * 2]);
          sendReport = true;
          bleGamepad.release(physicalButtons[encoderBtnStart + i * 2]);
        }
        break;

      case Encoder::anticlockwise:
        encoderDebouncers[i * 2 + 1].update();
        if (encoderDebouncers[i * 2 + 1].fell()) {
          bleGamepad.press(physicalButtons[encoderBtnStart + i * 2 + 1]);
          sendReport = true;
          bleGamepad.release(physicalButtons[encoderBtnStart + i * 2 + 1]);
        }
        break;

      default:
        break;
    }
  }
}

void setup() {
  setupButtons();
  setupEncoders();
  setupBleGamepad();
  for (byte i = 0; i < (NUM_OF_BUTTONS + NUM_OF_ENCODERS * 2); i++) {
    physicalButtons[i] = i + 1;
  }
  Serial.begin(115200);
}

void loop() {
  if (bleGamepad.isConnected()) {
    bool sendReport = false;
    processButtons(sendReport);
    processEncoders(sendReport);

    if (sendReport) bleGamepad.sendReport();
  } else {
    Serial.println("blegamepad not connected");
    delay(1000);
  }
  delay(10);
}