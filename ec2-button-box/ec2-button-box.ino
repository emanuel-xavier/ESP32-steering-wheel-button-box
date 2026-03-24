#define SERIAL_DEBUG

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

// ── Globals ─────────────────────────────────────────────────────────────────
static Config cfg;

Bounce       debouncers[NUM_OF_BUTTONS];
BleGamepad*  pBleGamepad = nullptr;

byte         buttonPins[NUM_OF_BUTTONS]        = {2, 13, 15, 14, 16, 17, 18, 19, 21, 22, 23, 25, 32, 33};
byte         encoderPins[NUM_OF_ENCODERS][2]   = {{26, 27}, {4, 5}};
#define MAX_ENC_BUTTONS  16  // supports up to 8 zones
byte         physicalButtons[NUM_OF_BUTTONS + MAX_ENC_BUTTONS];

Enc::Encoder     encoders[NUM_OF_ENCODERS];
const byte       encoderBtnStart = NUM_OF_BUTTONS;
volatile uint32_t buttonState    = 0;  // bitmask: bit i set = buttonPins[i] currently pressed

// ── Tasks ───────────────────────────────────────────────────────────────────
void buttonTask(void*) {
  while (true) {
    if (pBleGamepad->isConnected()) {
      bool dirty = false;
      for (byte i = 0; i < NUM_OF_BUTTONS; i++) {
        debouncers[i].update();
        if (debouncers[i].fell()) {
          buttonState |= (1u << i);
          pBleGamepad->press(physicalButtons[i]);
          dirty = true;
          #ifdef SERIAL_DEBUG
            Serial.printf("Button %d pressed  (pin %d)\n", physicalButtons[i], buttonPins[i]);
          #endif
        } else if (debouncers[i].rose()) {
          buttonState &= ~(1u << i);
          pBleGamepad->release(physicalButtons[i]);
          dirty = true;
          #ifdef SERIAL_DEBUG
            Serial.printf("Button %d released (pin %d)\n", physicalButtons[i], buttonPins[i]);
          #endif
        }
      }
      if (dirty) pBleGamepad->sendReport();
    }
    #ifdef SERIAL_DEBUG
    else { Serial.println("BLE not connected"); }
    #endif
    vTaskDelay(cfg.buttonTaskDelayMs / portTICK_PERIOD_MS);
  }
}

void encoderTask(void*) {
  while (true) {
    if (pBleGamepad->isConnected()) {
      for (int i = 0; i < NUM_OF_ENCODERS; i++) {
        Enc::Move m = encoders[i].read();
        if (m == Enc::none) continue;

        byte idx = encoderBtnStart + i * 2 + (m == Enc::ccw ? 1 : 0);
        #ifdef SERIAL_DEBUG
          Serial.printf("Encoder %d %s -> button %d (clk pin %d)\n",
            i, (m == Enc::ccw) ? "CCW" : "CW", physicalButtons[idx], encoderPins[i][0]);
        #endif
        pBleGamepad->press(physicalButtons[idx]);
        pBleGamepad->sendReport();
        vTaskDelay(cfg.encoderPressDurationMs / portTICK_PERIOD_MS);
        pBleGamepad->release(physicalButtons[idx]);
        pBleGamepad->sendReport();
      }
    }
    vTaskDelay(cfg.encoderTaskDelayMs / portTICK_PERIOD_MS);
  }
}

// Zones mode: encoder 1 tracks absolute position and determines the active zone.
// Encoder 2 triggers zone-specific buttons instead of fixed CW/CCW buttons.
// Button layout: physicalButtons[encoderBtnStart + zone*2 + (ccw?1:0)]
void encoderZonesTask(void*) {
  int  position    = 0;
  bool resetActive = false;  // prevents repeated resets while combo is held
  while (true) {
    if (pBleGamepad->isConnected()) {
      // Reset combo check
      uint32_t mask = cfg.encoderZoneResetMask;
      if (mask != 0 && (buttonState & mask) == mask) {
        if (!resetActive) {
          resetActive = true;
          position    = 0;
          #ifdef SERIAL_DEBUG
            Serial.println("Zone position reset to 0");
          #endif
        }
      } else {
        resetActive = false;
      }

      int master = (int)cfg.encoderZoneMaster;
      int slave  = 1 - master;

      Enc::Move m1 = encoders[master].read();
      if (m1 == Enc::cw)  position = (position + 1) % (int)cfg.encoderZoneSteps;
      if (m1 == Enc::ccw) position = (position - 1 + (int)cfg.encoderZoneSteps) % (int)cfg.encoderZoneSteps;

      int zone = (position * (int)cfg.encoderZoneCount) / (int)cfg.encoderZoneSteps;
      #ifdef SERIAL_DEBUG
        if (m1 != Enc::none)
          Serial.printf("Master enc%d %s -> pos %d / zone %d (clk pin %d)\n",
            master, (m1 == Enc::cw) ? "CW" : "CCW", position, zone, encoderPins[master][0]);
      #endif

      Enc::Move m2 = encoders[slave].read();
      if (m2 != Enc::none) {
        byte idx = encoderBtnStart + zone * 2 + (m2 == Enc::ccw ? 1 : 0);
        #ifdef SERIAL_DEBUG
          Serial.printf("Zone %d | Enc%d %s -> btn %d (clk pin %d)\n",
            zone, slave, (m2 == Enc::ccw) ? "CCW" : "CW", physicalButtons[idx], encoderPins[slave][0]);
        #endif
        pBleGamepad->press(physicalButtons[idx]);
        pBleGamepad->sendReport();
        vTaskDelay(cfg.encoderPressDurationMs / portTICK_PERIOD_MS);
        pBleGamepad->release(physicalButtons[idx]);
        pBleGamepad->sendReport();
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
  pBleGamepad = new BleGamepad(cfg.bleDeviceName.c_str(), "emanuelxavier.dev");
  BleGamepadConfiguration gcfg;
  gcfg.setButtonCount(NUM_OF_BUTTONS + encButtons);
  gcfg.setAutoReport(false);
  pBleGamepad->begin(&gcfg);
}

// ── Entry point ──────────────────────────────────────────────────────────────
void setup() {
  #ifdef SERIAL_DEBUG
    Serial.begin(115200);
  #endif

  cfg = loadConfig();

  // Config mode: hold the configured button while powering on (default: button 6, pin 17)
  uint32_t bootBtnIdx = constrain(cfg.configBootButton, 1u, (uint32_t)NUM_OF_BUTTONS) - 1;
  byte cfgPin = buttonPins[bootBtnIdx];
  pinMode(cfgPin, INPUT_PULLUP);
  delay(100); // let pin settle after power-on
  if (digitalRead(cfgPin) == LOW) {
    startConfigMode(); // never returns
  }

  // Crash-counter fallback: after 3 rapid reboots without a clean startup,
  // enter config mode automatically so the device is still recoverable.
  {
    Preferences bp;
    bp.begin("bootcnt", false);
    uint32_t cnt = bp.getUInt("cnt", 0) + 1;
    bp.putUInt("cnt", cnt);
    bp.end();
    delay(1000); // wait 1 s — a crash during this window keeps the counter high
    #ifdef SERIAL_DEBUG
      Serial.printf("[Boot] Boot counter: %u/3\n", cnt);
    #endif
    if (cnt >= 3) {
      #ifdef SERIAL_DEBUG
        Serial.println("[Boot] 3 rapid reboots detected — entering config mode");
      #endif
      Preferences bpReset;
      bpReset.begin("bootcnt", false);
      bpReset.putUInt("cnt", 0);
      bpReset.end();
      startConfigMode(); // never returns
    }
  }

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

  // Startup succeeded — clear crash counter
  {
    Preferences bp;
    bp.begin("bootcnt", false);
    bp.putUInt("cnt", 0);
    bp.end();
  }
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}
