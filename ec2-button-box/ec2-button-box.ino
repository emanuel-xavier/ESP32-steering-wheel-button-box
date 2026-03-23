// #define SERIAL_DEBUG

// ── Boot behaviour ─────────────────────────────────────────────────────────
// Hold button on pin CONFIG_BOOT_PIN (pin 2, i.e. the first button) while
// powering on to enter WiFi config mode. The device creates a WiFi AP named
// "ButtonBox-Config". Connect to that network and open http://192.168.4.1
// in any browser to read or update the persisted configuration.
// The device reboots automatically after saving.
//
// Normal boot (pin 2 not held): loads config from NVS and starts the gamepad.
//
// Required extra library: ArduinoJson >= 6.x  (Arduino Library Manager)

#include <Arduino.h>
#include <Bounce2.h>      // https://github.com/thomasfredericks/Bounce2
// ESP32-BLE-Gamepad 0.5.4
// NimBLE-Arduino    1.4.1
#include <BleGamepad.h>
#include "Config.h"
#include "ConfigWiFi.h"
#include "Encoder.h"

#define NUM_OF_BUTTONS   14
#define NUM_OF_ENCODERS   2
#define CONFIG_BOOT_PIN   25   // Hold LOW at boot to enter WiFi config mode

// ── Globals ─────────────────────────────────────────────────────────────────
static Config cfg;

Bounce       debouncers[NUM_OF_BUTTONS];
BleGamepad   bleGamepad("ESP32-steering-wheel", "emanuelxavier.dev");

byte         buttonPins[NUM_OF_BUTTONS]        = {2, 13, 15, 14, 16, 17, 18, 19, 21, 22, 23, 25, 32, 33};
byte         encoderPins[NUM_OF_ENCODERS][2]   = {{26, 27}, {4, 5}};
#define MAX_ENC_BUTTONS  16  // supports up to 8 zones
byte         physicalButtons[NUM_OF_BUTTONS + MAX_ENC_BUTTONS];

Enc::Encoder encoders[NUM_OF_ENCODERS];
const byte   encoderBtnStart = NUM_OF_BUTTONS;

// ── Tasks ───────────────────────────────────────────────────────────────────
void buttonTask(void*) {
  while (true) {
    if (bleGamepad.isConnected()) {
      bool dirty = false;
      for (byte i = 0; i < NUM_OF_BUTTONS; i++) {
        debouncers[i].update();
        if (debouncers[i].fell()) {
          bleGamepad.press(physicalButtons[i]);
          dirty = true;
          #ifdef SERIAL_DEBUG
            Serial.printf("Button %d pressed\n", physicalButtons[i]);
          #endif
        } else if (debouncers[i].rose()) {
          bleGamepad.release(physicalButtons[i]);
          dirty = true;
          #ifdef SERIAL_DEBUG
            Serial.printf("Button %d released\n", physicalButtons[i]);
          #endif
        }
      }
      if (dirty) bleGamepad.sendReport();
    }
    #ifdef SERIAL_DEBUG
    else { Serial.println("BLE not connected"); }
    #endif
    vTaskDelay(cfg.buttonTaskDelayMs / portTICK_PERIOD_MS);
  }
}

void encoderTask(void*) {
  while (true) {
    if (bleGamepad.isConnected()) {
      for (int i = 0; i < NUM_OF_ENCODERS; i++) {
        Enc::Move m = encoders[i].read();
        if (m == Enc::none) continue;

        byte idx = encoderBtnStart + i * 2 + (m == Enc::ccw ? 1 : 0);
        #ifdef SERIAL_DEBUG
          Serial.printf("Encoder %d %s -> button %d\n",
            i, (m == Enc::ccw) ? "CCW" : "CW", physicalButtons[idx]);
        #endif
        bleGamepad.press(physicalButtons[idx]);
        bleGamepad.sendReport();
        vTaskDelay(cfg.encoderPressDurationMs / portTICK_PERIOD_MS);
        bleGamepad.release(physicalButtons[idx]);
        bleGamepad.sendReport();
      }
    }
    vTaskDelay(cfg.encoderTaskDelayMs / portTICK_PERIOD_MS);
  }
}

// Zones mode: encoder 1 tracks absolute position and determines the active zone.
// Encoder 2 triggers zone-specific buttons instead of fixed CW/CCW buttons.
// Button layout: physicalButtons[encoderBtnStart + zone*2 + (ccw?1:0)]
void encoderZonesTask(void*) {
  int position = 0;
  while (true) {
    if (bleGamepad.isConnected()) {
      int master = (int)cfg.encoderZoneMaster;
      int slave  = 1 - master;

      Enc::Move m1 = encoders[master].read();
      if (m1 == Enc::cw)  position = (position + 1) % (int)cfg.encoderZoneSteps;
      if (m1 == Enc::ccw) position = (position - 1 + (int)cfg.encoderZoneSteps) % (int)cfg.encoderZoneSteps;

      int zone = (position * (int)cfg.encoderZoneCount) / (int)cfg.encoderZoneSteps;

      Enc::Move m2 = encoders[slave].read();
      if (m2 != Enc::none) {
        byte idx = encoderBtnStart + zone * 2 + (m2 == Enc::ccw ? 1 : 0);
        #ifdef SERIAL_DEBUG
          Serial.printf("Zone %d | Enc%d %s -> btn %d\n",
            zone, slave, (m2 == Enc::ccw) ? "CCW" : "CW", physicalButtons[idx]);
        #endif
        bleGamepad.press(physicalButtons[idx]);
        bleGamepad.sendReport();
        vTaskDelay(cfg.encoderPressDurationMs / portTICK_PERIOD_MS);
        bleGamepad.release(physicalButtons[idx]);
        bleGamepad.sendReport();
      }
    }
    vTaskDelay(cfg.encoderTaskDelayMs / portTICK_PERIOD_MS);
  }
}

// ── Setup helpers ────────────────────────────────────────────────────────────
void setupButtons() {
  for (byte i = 0; i < NUM_OF_BUTTONS; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
    debouncers[i].attach(buttonPins[i]);
    debouncers[i].interval(cfg.debounceDelayMs);
  }
}

void setupEncoders() {
  for (byte i = 0; i < NUM_OF_ENCODERS; i++) {
    encoders[i] = Enc::Encoder(encoderPins[i][0], encoderPins[i][1], cfg.encoderDebounceUs);
    encoders[i].begin();
  }
}

void setupBleGamepad() {
  int encButtons = 0;
  if (cfg.useEncoders)
    encButtons = cfg.encoderZonesMode ? (int)cfg.encoderZoneCount * 2 : NUM_OF_ENCODERS * 2;
  BleGamepadConfiguration gcfg;
  gcfg.setButtonCount(NUM_OF_BUTTONS + encButtons);
  gcfg.setAutoReport(false);
  bleGamepad.begin(&gcfg);
}

// ── Entry point ──────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // Config mode: hold first button (pin 2) LOW while powering on
  pinMode(CONFIG_BOOT_PIN, INPUT_PULLUP);
  delay(100); // let pin settle after power-on
  if (digitalRead(CONFIG_BOOT_PIN) == LOW) {
    startConfigMode(); // never returns
  }

  cfg = loadConfig();

  setupButtons();
  if (cfg.useEncoders) setupEncoders();
  setupBleGamepad();

  for (byte i = 0; i < NUM_OF_BUTTONS + MAX_ENC_BUTTONS; i++)
    physicalButtons[i] = i + 1;

  xTaskCreate(buttonTask, "ButtonTask", 4096, NULL, 1, NULL);
  if (cfg.useEncoders) {
    if (cfg.encoderZonesMode)
      xTaskCreate(encoderZonesTask, "EncZonesTask", 4096, NULL, 1, NULL);
    else
      xTaskCreate(encoderTask, "EncoderTask", 4096, NULL, 1, NULL);
  }
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}
