#pragma once
// Always-on BLE configuration service.
// Attached to the same NimBLE server as the gamepad — no special boot mode needed.
// Connect with the desktop app at any time to read or update configuration.
//
// Service UUID : bb010000-feed-dead-beef-cafebabe0001
//   config_read  (READ)   : bb010001  → current config JSON (always fresh)
//   config_write (WRITE)  : bb010002  → new config JSON, saved to NVS
//   reboot       (WRITE)  : bb010003  → any write triggers esp_restart()
//   ota_trigger  (WRITE)  : bb010004  → any write enters OTA WiFi mode
//   btn_events   (NOTIFY) : bb010005  → 2-byte packets: [press(1)/release(0), btnNumber]

#include <NimBLEDevice.h>
#include "Config.h"
#include "OTA.h"

#define BB_SERVICE_UUID      "bb010000-feed-dead-beef-cafebabe0001"
#define BB_CFG_READ_UUID     "bb010001-feed-dead-beef-cafebabe0001"
#define BB_CFG_WRITE_UUID    "bb010002-feed-dead-beef-cafebabe0001"
#define BB_CFG_REBOOT_UUID   "bb010003-feed-dead-beef-cafebabe0001"
#define BB_OTA_TRIGGER_UUID  "bb010004-feed-dead-beef-cafebabe0001"
#define BB_BTN_NOTIFY_UUID   "bb010005-feed-dead-beef-cafebabe0001"

// Global pointers set during registerConfigService().
static NimBLECharacteristic* _pBtnNotify = nullptr;
static NimBLECharacteristic* _pCfgRead   = nullptr;

// Call from any task when a button is pressed or released.
// btnNumber is the 1-based gamepad button number (same as physicalButtons[i]).
inline void notifyButtonEvent(uint8_t btnNumber, bool press) {
  if (_pBtnNotify == nullptr) return;
  uint8_t buf[2] = {(uint8_t)(press ? 1 : 0), btnNumber};
  _pBtnNotify->notify(buf, sizeof(buf));
}

// No onRead callback — value is pre-populated at boot and refreshed after each save.
// Calling loadConfig() inside the NimBLE host task's onRead overflows its stack
// (Preferences NVS work is too heavy), causing ATT error 0x0e and killing advertising.

// Chunked-write reassembly buffer (shared across calls within one transaction).
static String _writeBuf;

class _BbWriteCallbacks : public NimBLECharacteristicCallbacks {
  // Protocol: byte 0 is a command, remaining bytes are payload.
  //   0x01 = start  (clear buffer, append payload)
  //   0x02 = continue (append payload)
  //   0x03 = end     (append payload, then save to NVS)
  void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo&) override {
    std::string val = pChar->getValue();
    if (val.empty()) return;
    uint8_t cmd = (uint8_t)val[0];
    String chunk = String(val.c_str() + 1, val.size() - 1);

    if (cmd == 0x01) {
      _writeBuf = chunk;
    } else if (cmd == 0x02) {
      _writeBuf += chunk;
    } else if (cmd == 0x03) {
      _writeBuf += chunk;
      Config cfg = loadConfig();
      if (jsonToConfig(_writeBuf, cfg)) {
        cfg.recoveryOccurred = false;  // user saved successfully — clear the recovery flag
        saveConfig(cfg);
        if (_pCfgRead) _pCfgRead->setValue(configToJson(cfg).c_str());
        #ifdef SERIAL_DEBUG
          Serial.println("[BLE Config] Config saved to NVS.");
        #endif
      } else {
        #ifdef SERIAL_DEBUG
          Serial.println("[BLE Config] Received invalid JSON — ignored.");
        #endif
      }
      _writeBuf = "";
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
// together when the NimBLE host syncs.
inline void registerConfigService(NimBLEServer* pServer, const Config& bootCfg) {
  NimBLEDevice::setMTU(512);

  NimBLEService* pService = pServer->createService(BB_SERVICE_UUID);

  _pCfgRead = pService->createCharacteristic(
    BB_CFG_READ_UUID, NIMBLE_PROPERTY::READ);
  // Value pre-populated here; no onRead callback (see note above).
  // After a save, onWrite refreshes _pCfgRead so the next read is current.
  _pCfgRead->setValue(configToJson(bootCfg).c_str());

  NimBLECharacteristic* pWrite = pService->createCharacteristic(
    BB_CFG_WRITE_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  pWrite->setCallbacks(new _BbWriteCallbacks());

  NimBLECharacteristic* pReboot = pService->createCharacteristic(
    BB_CFG_REBOOT_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  pReboot->setCallbacks(new _BbRebootCallbacks());

  NimBLECharacteristic* pOta = pService->createCharacteristic(
    BB_OTA_TRIGGER_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  pOta->setCallbacks(new _BbOtaCallbacks());

  _pBtnNotify = pService->createCharacteristic(
    BB_BTN_NOTIFY_UUID, NIMBLE_PROPERTY::NOTIFY);

  pService->start();

  NimBLEDevice::getAdvertising()->addServiceUUID(BB_SERVICE_UUID);

  #ifdef SERIAL_DEBUG
    Serial.println("[BLE Config] Config service registered.");
  #endif
}
