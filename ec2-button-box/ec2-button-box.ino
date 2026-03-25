#define SERIAL_DEBUG

// The BLE config service runs alongside the gamepad at all times.
// Open the desktop app, click Scan, and configure without any special boot mode.
//
// Required extra library: ArduinoJson >= 6.x  (Arduino Library Manager)

#include <Arduino.h>
#include <Bounce2.h>      // https://github.com/thomasfredericks/Bounce2
// ESP32-BLE-Gamepad 0.5.4
// NimBLE-Arduino    1.4.1
#include <BleGamepad.h>
#include "Config.h"
#include "ConfigBLE.h"
#include "Encoder.h"

#define MAX_BUTTONS     128  // direct(32) + matrix(64) + encoders(16) + headroom
#define NUM_OF_ENCODERS   2

// ── Globals ─────────────────────────────────────────────────────────────────
static Config cfg;

Bounce       debouncers[MAX_BUTTONS];
BleGamepad*  pBleGamepad = nullptr;

#define MAX_ENC_BUTTONS  16  // supports up to 8 zones
byte         physicalButtons[MAX_BUTTONS + MAX_ENC_BUTTONS];

Enc::Encoder     encoders[NUM_OF_ENCODERS];
volatile uint32_t buttonState    = 0;  // bitmask: bit i set = buttonPins[i] currently pressed

// First physicalButtons index reserved for encoder buttons (after direct + matrix buttons)
inline byte encBtnBase() {
  return cfg.numButtons + (cfg.useMatrix ? (int)cfg.matrixRows * cfg.matrixCols : 0);
}

// ── Tasks ───────────────────────────────────────────────────────────────────
void buttonTask(void*) {
  while (true) {
    if (pBleGamepad->isConnected()) {
      bool dirty = false;
      for (byte i = 0; i < cfg.numButtons; i++) {
        debouncers[i].update();
        if (debouncers[i].fell()) {
          buttonState |= (1u << i);
          pBleGamepad->press(physicalButtons[i]);
          notifyButtonEvent(physicalButtons[i], true);
          dirty = true;
          #ifdef SERIAL_DEBUG
            Serial.printf("Button %d pressed  (pin %d)\n", physicalButtons[i], cfg.buttonPins[i]);
          #endif
        } else if (debouncers[i].rose()) {
          buttonState &= ~(1u << i);
          pBleGamepad->release(physicalButtons[i]);
          notifyButtonEvent(physicalButtons[i], false);
          dirty = true;
          #ifdef SERIAL_DEBUG
            Serial.printf("Button %d released (pin %d)\n", physicalButtons[i], cfg.buttonPins[i]);
          #endif
        }
      }
      if (dirty) pBleGamepad->sendReport();

      // ── Matrix scan ──────────────────────────────────────────────────────
      if (cfg.useMatrix) {
        if (cfg.matrixDirectMode) {
          // Direct mode: all pins are INPUT_PULLUP, no row driving.
          // Button (r,c) fires only when BOTH row[r] AND col[c] read LOW simultaneously.
          // Same rows×cols button count as scanned mode.
          static bool directState[8][8] = {};
          for (uint8_t r = 0; r < cfg.matrixRows; r++) {
            bool rowLow = (digitalRead(cfg.matrixRowPins[r]) == LOW);
            for (uint8_t c = 0; c < cfg.matrixCols; c++) {
              bool pressed = rowLow && (digitalRead(cfg.matrixColPins[c]) == LOW);
              if (pressed != directState[r][c]) {
                directState[r][c] = pressed;
                byte bleBtn = physicalButtons[cfg.numButtons + r * cfg.matrixCols + c];
                if (pressed) { pBleGamepad->press(bleBtn);   notifyButtonEvent(bleBtn, true);  }
                else         { pBleGamepad->release(bleBtn); notifyButtonEvent(bleBtn, false); }
                pBleGamepad->sendReport();
                #ifdef SERIAL_DEBUG
                  Serial.printf("Direct [%d][%d] %s -> button %d\n", r, c, pressed ? "pressed" : "released", bleBtn);
                #endif
              }
            }
          }
        } else {
          static bool matrixState[8][8] = {};
          for (uint8_t r = 0; r < cfg.matrixRows; r++) {
            digitalWrite(cfg.matrixRowPins[r], LOW);
            delayMicroseconds(10);
            for (uint8_t c = 0; c < cfg.matrixCols; c++) {
              bool pressed = (digitalRead(cfg.matrixColPins[c]) == LOW);
              if (pressed != matrixState[r][c]) {
                matrixState[r][c] = pressed;
                byte bleBtn = physicalButtons[cfg.numButtons + r * cfg.matrixCols + c];
                if (pressed) { pBleGamepad->press(bleBtn);   notifyButtonEvent(bleBtn, true);  }
                else         { pBleGamepad->release(bleBtn); notifyButtonEvent(bleBtn, false); }
                pBleGamepad->sendReport();
                #ifdef SERIAL_DEBUG
                  Serial.printf("Matrix [%d][%d] %s -> button %d\n", r, c, pressed ? "pressed" : "released", bleBtn);
                #endif
              }
            }
            digitalWrite(cfg.matrixRowPins[r], HIGH);
          }
        }
      }
    }
    #ifdef SERIAL_DEBUG
      else { Serial.println("BLE not connected"); }
      delayMicroseconds(1000);
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

        byte idx = encBtnBase() + i * 2 + (m == Enc::ccw ? 1 : 0);
        #ifdef SERIAL_DEBUG
          Serial.printf("Encoder %d %s -> button %d (clk pin %d)\n",
            i, (m == Enc::ccw) ? "CCW" : "CW", physicalButtons[idx], cfg.encoderPins[i][0]);
        #endif
        pBleGamepad->press(physicalButtons[idx]);
        notifyButtonEvent(physicalButtons[idx], true);
        pBleGamepad->sendReport();
        vTaskDelay(cfg.encoderPressDurationMs / portTICK_PERIOD_MS);
        pBleGamepad->release(physicalButtons[idx]);
        notifyButtonEvent(physicalButtons[idx], false);
        pBleGamepad->sendReport();
      }
    }
    vTaskDelay(cfg.encoderTaskDelayMs / portTICK_PERIOD_MS);
  }
}

// Zones mode: encoder 1 tracks absolute position and determines the active zone.
// Encoder 2 triggers zone-specific buttons instead of fixed CW/CCW buttons.
// Button layout: physicalButtons[cfg.numButtons + zone*2 + (ccw?1:0)]
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
            master, (m1 == Enc::cw) ? "CW" : "CCW", position, zone, cfg.encoderPins[master][0]);
      #endif

      Enc::Move m2 = encoders[slave].read();
      if (m2 != Enc::none) {
        byte idx = encBtnBase() + zone * 2 + (m2 == Enc::ccw ? 1 : 0);
        #ifdef SERIAL_DEBUG
          Serial.printf("Zone %d | Enc%d %s -> btn %d (clk pin %d)\n",
            zone, slave, (m2 == Enc::ccw) ? "CCW" : "CW", physicalButtons[idx], cfg.encoderPins[slave][0]);
        #endif
        pBleGamepad->press(physicalButtons[idx]);
        notifyButtonEvent(physicalButtons[idx], true);
        pBleGamepad->sendReport();
        vTaskDelay(cfg.encoderPressDurationMs / portTICK_PERIOD_MS);
        pBleGamepad->release(physicalButtons[idx]);
        notifyButtonEvent(physicalButtons[idx], false);
        pBleGamepad->sendReport();
      }
    }
    vTaskDelay(cfg.encoderTaskDelayMs / portTICK_PERIOD_MS);
  }
}

// ── Setup helpers ────────────────────────────────────────────────────────────
void setupButtons() {
  for (byte i = 0; i < cfg.numButtons; i++) {
    pinMode(cfg.buttonPins[i], INPUT_PULLUP);
    debouncers[i].attach(cfg.buttonPins[i]);
    debouncers[i].interval(cfg.debounceDelayMs);
  }
}

void setupMatrix() {
  if (cfg.matrixDirectMode) {
    // All row + col pins are independent INPUT_PULLUP buttons (no scanning).
    for (uint8_t r = 0; r < cfg.matrixRows; r++) pinMode(cfg.matrixRowPins[r], INPUT_PULLUP);
    for (uint8_t c = 0; c < cfg.matrixCols; c++) pinMode(cfg.matrixColPins[c], INPUT_PULLUP);
  } else {
    for (uint8_t c = 0; c < cfg.matrixCols; c++) pinMode(cfg.matrixColPins[c], INPUT_PULLUP);
    for (uint8_t r = 0; r < cfg.matrixRows; r++) {
      pinMode(cfg.matrixRowPins[r], OUTPUT);
      digitalWrite(cfg.matrixRowPins[r], HIGH);  // idle HIGH — not selecting any row
    }
  }
}

void setupEncoders() {
  for (byte i = 0; i < NUM_OF_ENCODERS; i++) {
    encoders[i] = Enc::Encoder(cfg.encoderPins[i][0], cfg.encoderPins[i][1], cfg.encoderDebounceUs);
    encoders[i].begin();
  }
}

void setupBleGamepad() {
  int encButtons = 0;
  if (cfg.useEncoders)
    encButtons = cfg.encoderZonesMode ? (int)cfg.encoderZoneCount * 2 : NUM_OF_ENCODERS * 2;
  int matButtons = cfg.useMatrix ? (int)cfg.matrixRows * cfg.matrixCols : 0;
  pBleGamepad = new BleGamepad(cfg.bleDeviceName.c_str(), "emanuelxavier.dev");
  BleGamepadConfiguration gcfg;
  gcfg.setButtonCount(cfg.numButtons + matButtons + encButtons);
  gcfg.setAutoReport(false);
  pBleGamepad->begin(&gcfg);
}

// ── Entry point ──────────────────────────────────────────────────────────────
void setup() {
  #ifdef SERIAL_DEBUG
    Serial.begin(115200);
  #endif

  // ── Crash-loop detection ─────────────────────────────────────────────────
  // Count consecutive abnormal resets (WDT / panic). If >= 3 in a row,
  // the current config is likely causing an OOM crash — reset to defaults
  // and notify the user via the desktop app's recoveryOccurred flag.
  {
    esp_reset_reason_t reason = esp_reset_reason();
    bool abnormal = (reason == ESP_RST_TASK_WDT ||
                     reason == ESP_RST_INT_WDT  ||
                     reason == ESP_RST_WDT       ||
                     reason == ESP_RST_PANIC);
    Preferences cp;
    cp.begin("bbcrash", false);
    uint8_t crashes = cp.getUChar("count", 0);
    if (abnormal) {
      crashes++;
      cp.putUChar("count", crashes);
    } else {
      crashes = 0;
      cp.putUChar("count", 0);
    }
    cp.end();

    if (crashes >= 3) {
      cfg = Config{};
      cfg.recoveryOccurred = true;
      saveConfig(cfg);          // persist defaults + recovered=true
      Preferences cp2;
      cp2.begin("bbcrash", false);
      cp2.putUChar("count", 0); // reset crash counter
      cp2.end();
      #ifdef SERIAL_DEBUG
        Serial.println("[Recovery] Crash-loop detected — config reset to defaults.");
      #endif
    } else {
      cfg = loadConfig();
    }
  }
  setupButtons();
  if (cfg.useMatrix)   setupMatrix();
  if (cfg.useEncoders) setupEncoders();

  // Init NimBLE and register the config service BEFORE setupBleGamepad().
  // All services must be created before BleGamepad::begin() finalises the GATT table.
  NimBLEDevice::init(cfg.bleDeviceName.c_str());
  registerConfigService(NimBLEDevice::createServer(), cfg);

  setupBleGamepad(); // adds HID service to the same server, then starts advertising

  for (byte i = 0; i < MAX_BUTTONS + MAX_ENC_BUTTONS; i++)
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
