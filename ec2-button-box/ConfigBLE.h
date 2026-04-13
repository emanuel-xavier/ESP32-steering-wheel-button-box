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
//   enc_ctrl     (WRITE)  : bb010006  → 0x00 = disable encoders, 0x01 = enable encoders

#include <NimBLEDevice.h>
#include "Config.h"
#include "OTA.h"

#define BB_SERVICE_UUID      "bb010000-feed-dead-beef-cafebabe0001"
#define BB_CFG_READ_UUID     "bb010001-feed-dead-beef-cafebabe0001"
#define BB_CFG_WRITE_UUID    "bb010002-feed-dead-beef-cafebabe0001"
#define BB_CFG_REBOOT_UUID   "bb010003-feed-dead-beef-cafebabe0001"
#define BB_OTA_TRIGGER_UUID  "bb010004-feed-dead-beef-cafebabe0001"
#define BB_BTN_NOTIFY_UUID   "bb010005-feed-dead-beef-cafebabe0001"
#define BB_ENC_CTRL_UUID     "bb010006-feed-dead-beef-cafebabe0001"

// Defined in ec2-button-box.ino — toggled at runtime via BB_ENC_CTRL_UUID writes.
extern volatile bool encodersEnabled;

// Global pointers set during registerConfigService().
static NimBLECharacteristic* _pBtnNotify = nullptr;
static NimBLECharacteristic* _pCfgRead   = nullptr;

// Pre-serialised JSON cache — populated at boot and after each successful save.
// Pushed to the client as a NOTIFY when they connect (avoids ATT READ issues with BlueZ).
static String _cfgJsonCache;

// Send _cfgJsonCache as chunked notifications on _pCfgRead.
// Uses the same 0x01/0x02/0x03 framing as chunked writes so the Go side can reassemble.
// Safe to call from any FreeRTOS task (not from an ISR).
static void _sendConfigNotify() {
  if (_pCfgRead == nullptr || _cfgJsonCache.length() == 0) return;
  const char* data  = _cfgJsonCache.c_str();
  size_t       total = _cfgJsonCache.length();
  const size_t kChunk = 180;
  static uint8_t pkt[181];  // BSS — avoids stack pressure inside the task
  for (size_t offset = 0; offset < total; ) {
    size_t end = offset + kChunk;
    if (end > total) end = total;
    uint8_t cmd = (offset == 0 && end == total) ? 0x03
                : (offset == 0)                 ? 0x01
                : (end   == total)              ? 0x03
                                                : 0x02;
    pkt[0] = cmd;
    memcpy(pkt + 1, data + offset, end - offset);
    _pCfgRead->notify(pkt, 1 + (end - offset));
    offset = end;
    if (cmd != 0x03) vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}

// One-shot FreeRTOS task: waits for the client to subscribe to the CCCD, then pushes config.
static void _cfgNotifyTask(void* pvMs) {
  uint32_t delayMs = (uint32_t)(uintptr_t)pvMs;
  vTaskDelay(delayMs / portTICK_PERIOD_MS);
  _sendConfigNotify();
  vTaskDelete(nullptr);
}

// Call from any task when a button is pressed or released.
// btnNumber is the 1-based gamepad button number (same as physicalButtons[i]).
inline void notifyButtonEvent(uint8_t btnNumber, bool press) {
  if (_pBtnNotify == nullptr) return;
  uint8_t buf[2] = {(uint8_t)(press ? 1 : 0), btnNumber};
  _pBtnNotify->notify(buf, sizeof(buf));
}

// Characteristic callbacks for _pCfgRead.
// onRead: serves the cached JSON for clients that do an ATT READ.
// onSubscribe: fires when the Go client writes to the CCCD — we send the config
//   as a NOTIFY at this point, which is guaranteed to arrive (unlike onConnect
//   which BleGamepad::begin() overwrites with its own server callbacks).
class _BbReadCallbacks : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* pChar, NimBLEConnInfo&) override {
    pChar->setValue((uint8_t*)_cfgJsonCache.c_str(), _cfgJsonCache.length());
    #ifdef SERIAL_DEBUG
      // Serial.printf("[BLE Config] onRead: serving %u bytes\n", _cfgJsonCache.length());
    #endif
  }
  void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&, uint16_t subValue) override {
    if (subValue == 0) return;  // client unsubscribed
    #ifdef SERIAL_DEBUG
      Serial.println("[BLE Config] CCCD subscribed — scheduling config notify (200ms).");
    #endif
    xTaskCreate(_cfgNotifyTask, "CfgNotify", 4096, (void*)200UL, 1, nullptr);
  }
};

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
      #ifdef SERIAL_DEBUG
        Serial.print("[BLE Config] Received JSON ("); Serial.print(_writeBuf.length()); Serial.println(" bytes):");
        Serial.println(_writeBuf);
      #endif
      Config cfg = loadConfig();
      if (jsonToConfig(_writeBuf, cfg)) {
        cfg.recoveryOccurred = false;  // user saved successfully — clear the recovery flag
        saveConfig(cfg);
        _cfgJsonCache = configToJson(cfg);
        #ifdef SERIAL_DEBUG
          Serial.println("[BLE Config] Saved. Key values:");
          Serial.print("  numButtons="); Serial.println(cfg.numButtons);
          Serial.print("  useEncoders="); Serial.println(cfg.useEncoders);
          Serial.print("  useMatrix="); Serial.println(cfg.useMatrix);
          Serial.print("  bleDeviceName="); Serial.println(cfg.bleDeviceName);
        #endif
      } else {
        #ifdef SERIAL_DEBUG
          Serial.println("[BLE Config] jsonToConfig failed — invalid or oversized JSON.");
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

class _BbEncCtrlCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo&) override {
    std::string val = pChar->getValue();
    if (val.empty()) return;
    encodersEnabled = (val[0] != 0);
    #ifdef SERIAL_DEBUG
      Serial.printf("[BLE Config] Encoders %s at runtime.\n", encodersEnabled ? "enabled" : "disabled");
    #endif
  }
};

// Register the config GATT service on the NimBLE server.
// MUST be called BEFORE BleGamepad::begin() so all services are finalized
// together when the NimBLE host syncs.
inline void registerConfigService(NimBLEServer* pServer, const Config& bootCfg) {
  NimBLEDevice::setMTU(512);

  NimBLEService* pService = pServer->createService(BB_SERVICE_UUID);

  _pCfgRead = pService->createCharacteristic(
    BB_CFG_READ_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  _pCfgRead->setCallbacks(new _BbReadCallbacks());

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

  NimBLECharacteristic* pEncCtrl = pService->createCharacteristic(
    BB_ENC_CTRL_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  pEncCtrl->setCallbacks(new _BbEncCtrlCallbacks());

  pService->start();

  // Populate the JSON cache after start() so the onRead callback has data.
  _cfgJsonCache = configToJson(bootCfg);
  #ifdef SERIAL_DEBUG
    Serial.printf("[BLE Config] JSON cache len=%u first32='%.32s'\n",
                  _cfgJsonCache.length(), _cfgJsonCache.c_str());
  #endif

  NimBLEDevice::getAdvertising()->addServiceUUID(BB_SERVICE_UUID);

  #ifdef SERIAL_DEBUG
    Serial.println("[BLE Config] Config service registered.");
  #endif
}
