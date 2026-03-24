#pragma once
// Always-on BLE configuration service.
// Attached to the same NimBLE server as the gamepad — no special boot mode needed.
// Connect with the desktop app at any time to read or update configuration.
//
// Service UUID : bb010000-feed-dead-beef-cafebabe0001
//   config_read  (READ)  : bb010001-feed-dead-beef-cafebabe0001  → current config JSON (always fresh)
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

class _BbReadCallbacks : public NimBLECharacteristicCallbacks {
  // Always serve fresh config from NVS so the desktop app sees current state.
  void onRead(NimBLECharacteristic* pChar, NimBLEConnInfo&) override {
    pChar->setValue(configToJson(loadConfig()).c_str());
  }
};

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

// Register the config GATT service on the NimBLE server.
// MUST be called BEFORE BleGamepad::begin() so all services are finalized
// together when the NimBLE host syncs. Calling pService->start() after the
// GATT table is frozen causes a NimBLE mutex assert crash.
inline void registerConfigService(NimBLEServer* pServer) {
  NimBLEDevice::setMTU(512);

  NimBLEService* pService = pServer->createService(BB_SERVICE_UUID);

  NimBLECharacteristic* pRead = pService->createCharacteristic(
    BB_CFG_READ_UUID, NIMBLE_PROPERTY::READ);
  pRead->setCallbacks(new _BbReadCallbacks());

  NimBLECharacteristic* pWrite = pService->createCharacteristic(
    BB_CFG_WRITE_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  pWrite->setCallbacks(new _BbWriteCallbacks());

  NimBLECharacteristic* pReboot = pService->createCharacteristic(
    BB_CFG_REBOOT_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  pReboot->setCallbacks(new _BbRebootCallbacks());

  NimBLECharacteristic* pOta = pService->createCharacteristic(
    BB_OTA_TRIGGER_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  pOta->setCallbacks(new _BbOtaCallbacks());

  pService->start();

  // Queue the UUID for advertisements — BleGamepad::begin() will call
  // pAdvertising->start() later, picking this up automatically.
  NimBLEDevice::getAdvertising()->addServiceUUID(BB_SERVICE_UUID);

  #ifdef SERIAL_DEBUG
    Serial.println("[BLE Config] Config service registered.");
  #endif
}
