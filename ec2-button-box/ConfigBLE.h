#pragma once
// BLE GATT Configuration Mode
// Hold the config boot button while powering on to enter this mode.
// The ESP32 advertises as "ButtonBox-Config"; connect with the desktop app.
//
// No extra libraries needed — NimBLE-Arduino is already required by the gamepad.
//
// Service UUID : bb010000-feed-dead-beef-cafebabe0001
//   config_read  (READ)  : bb010001-feed-dead-beef-cafebabe0001  → current config JSON
//   config_write (WRITE) : bb010002-feed-dead-beef-cafebabe0001  → new config JSON, saved to NVS
//   reboot       (WRITE) : bb010003-feed-dead-beef-cafebabe0001  → any write triggers esp_restart()
//   ota_trigger  (WRITE) : bb010004-feed-dead-beef-cafebabe0001  → any write enters OTA WiFi mode

#include <NimBLEDevice.h>
#include "Config.h"
#include "OTA.h"

#define BB_SERVICE_UUID      "bb010000-feed-dead-beef-cafebabe0001"
#define BB_CFG_READ_UUID     "bb010001-feed-dead-beef-cafebabe0001"
#define BB_CFG_WRITE_UUID    "bb010002-feed-dead-beef-cafebabe0001"
#define BB_CFG_REBOOT_UUID   "bb010003-feed-dead-beef-cafebabe0001"
#define BB_OTA_TRIGGER_UUID  "bb010004-feed-dead-beef-cafebabe0001"

class _BbWriteCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo&) override {
    std::string val = pChar->getValue();
    if (val.empty()) return;
    Config cfg = loadConfig();
    if (jsonToConfig(String(val.c_str()), cfg)) {
      saveConfig(cfg);
      #ifdef SERIAL_DEBUG
        Serial.println("[BLE Config] Config saved to NVS.");
      #endif
    } else {
      #ifdef SERIAL_DEBUG
        Serial.println("[BLE Config] Received invalid JSON — ignored.");
      #endif
    }
  }
};

class _BbRebootCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic*, NimBLEConnInfo&) override {
    #ifdef SERIAL_DEBUG
      Serial.println("[BLE Config] Reboot requested.");
    #endif
    delay(500);
    esp_restart();
  }
};

class _BbOtaCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic*, NimBLEConnInfo&) override {
    #ifdef SERIAL_DEBUG
      Serial.println("[BLE Config] OTA mode requested — switching to WiFi AP.");
    #endif
    Config cfg = loadConfig();
    startOTAMode(cfg.otaPassword);  // never returns
  }
};

inline void startConfigMode() {
  #ifdef SERIAL_DEBUG
    Serial.begin(115200);
    Serial.println("[BLE Config] Starting — advertising as 'ButtonBox-Config'");
  #endif

  NimBLEDevice::init("ButtonBox-Config");
  NimBLEDevice::setMTU(512);  // negotiate up to 512 bytes — config JSON fits in one write

  NimBLEServer*  pServer  = NimBLEDevice::createServer();
  NimBLEService* pService = pServer->createService(BB_SERVICE_UUID);

  // Read: returns current config as JSON
  NimBLECharacteristic* pRead = pService->createCharacteristic(
    BB_CFG_READ_UUID, NIMBLE_PROPERTY::READ);
  pRead->setValue(configToJson(loadConfig()).c_str());

  // Write: accepts new config JSON, saves to NVS
  // WRITE_NR (no-response) is included so BlueZ on Linux can use ATT Write Command
  // after MTU negotiation — the config JSON (~350 bytes) fits in one PDU at MTU=512.
  NimBLECharacteristic* pWrite = pService->createCharacteristic(
    BB_CFG_WRITE_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  pWrite->setCallbacks(new _BbWriteCallbacks());

  // Reboot: any write triggers esp_restart()
  NimBLECharacteristic* pReboot = pService->createCharacteristic(
    BB_CFG_REBOOT_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  pReboot->setCallbacks(new _BbRebootCallbacks());

  // OTA trigger: any write stops BLE and starts WiFi ArduinoOTA soft-AP
  NimBLECharacteristic* pOta = pService->createCharacteristic(
    BB_OTA_TRIGGER_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  pOta->setCallbacks(new _BbOtaCallbacks());

  pService->start();

  NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
  pAdv->addServiceUUID(BB_SERVICE_UUID);
  pAdv->start();

  #ifdef SERIAL_DEBUG
    Serial.println("[BLE Config] Ready. Open the desktop app to configure.");
  #endif

  while (true) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}
