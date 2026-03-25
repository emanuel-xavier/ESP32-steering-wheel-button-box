#pragma once
// Requires: ArduinoJson >= 6.x  (install via Library Manager)
#include <Preferences.h>
#include <ArduinoJson.h>

struct Config {
  String   bleDeviceName          = "ESP32-steering-wheel";
  uint8_t  numButtons             = 14;    // Number of physical buttons (0–32)
  String   otaPassword            = "";    // ArduinoOTA password (empty = no password)
  bool     useEncoders            = true;
  uint32_t debounceDelayMs        = 5;     // Bounce2 button debounce (ms)
  uint32_t encoderDebounceUs      = 1000;  // Encoder debounce (µs)
  uint32_t buttonTaskDelayMs      = 5;     // buttonTask polling interval (ms)
  uint32_t encoderPressDurationMs = 100;   // Simulated encoder key-press duration (ms)
  uint32_t encoderTaskDelayMs     = 5;     // encoderTask polling interval (ms)
  // Zones mode: encoder 1 selects the active zone; encoder 2 triggers zone-mapped buttons.
  // Mutually exclusive with normal encoder mode — select one via encoderZonesMode.
  bool     encoderZonesMode       = false;
  uint32_t encoderZoneMaster      = 0;     // index of the selector encoder (0 or 1)
  uint32_t encoderZoneSteps       = 20;    // total steps of the selector encoder
  uint32_t encoderZoneCount       = 2;     // number of zones to divide those steps into
  // Bitmask of physical buttons (bit N = button N+1) that must all be held simultaneously
  // to reset the master encoder position to 0. 0 = feature disabled.
  uint32_t encoderZoneResetMask   = 0;
  // Pin assignments — GPIO numbers for each button and encoder channel
  uint8_t  buttonPins[32]    = {2, 13, 15, 14, 16, 17, 18, 19, 21, 22, 23, 25, 32, 33};
  uint8_t  encoderPins[2][2] = {{26, 27}, {4, 5}};  // [enc][clk=0/dt=1]
  // Button matrix — row/col scanning, active alongside direct buttons
  bool     useMatrix          = false;
  // directMode: all row + col pins become independent INPUT_PULLUP buttons (rows+cols total).
  // No scanning, no ghosting, but fewer buttons per pin than scanned mode.
  bool     matrixDirectMode   = false;
  uint8_t  matrixRows         = 4;
  uint8_t  matrixCols         = 4;
  uint8_t  matrixRowPins[8]   = {};   // OUTPUT in scan mode, INPUT_PULLUP in direct mode
  uint8_t  matrixColPins[8]   = {};   // INPUT_PULLUP in both modes
  // Set true when a crash-loop was detected at boot and config was reset to defaults.
  // Reported to the desktop app via JSON; cleared on the next successful save.
  bool     recoveryOccurred   = false;
};

inline Config loadConfig() {
  Config cfg;
  Preferences prefs;
  if (!prefs.begin("buttonbox", true)) return cfg;  // returns defaults if no NVS entry
  cfg.bleDeviceName          = prefs.getString("bleName",      cfg.bleDeviceName);
  cfg.numButtons             = prefs.getUChar("numButtons",   cfg.numButtons);
  cfg.otaPassword            = prefs.getString("otaPassword", cfg.otaPassword);
  cfg.useEncoders            = prefs.getBool("useEncoders",    cfg.useEncoders);
  cfg.debounceDelayMs        = prefs.getUInt("debounceMs",     cfg.debounceDelayMs);
  cfg.encoderDebounceUs      = prefs.getUInt("encDebounceUs",  cfg.encoderDebounceUs);
  cfg.buttonTaskDelayMs      = prefs.getUInt("btnTaskMs",      cfg.buttonTaskDelayMs);
  cfg.encoderPressDurationMs = prefs.getUInt("encPressMs",     cfg.encoderPressDurationMs);
  cfg.encoderTaskDelayMs     = prefs.getUInt("encTaskMs",      cfg.encoderTaskDelayMs);
  cfg.encoderZonesMode       = prefs.getBool("encZonesMode",   cfg.encoderZonesMode);
  cfg.encoderZoneMaster      = prefs.getUInt("encZoneMaster",  cfg.encoderZoneMaster);
  cfg.encoderZoneSteps       = prefs.getUInt("encZoneSteps",   cfg.encoderZoneSteps);
  cfg.encoderZoneCount       = prefs.getUInt("encZoneCount",   cfg.encoderZoneCount);
  cfg.encoderZoneResetMask   = prefs.getUInt("encResetMask",   cfg.encoderZoneResetMask);
  prefs.getBytes("btnPins", cfg.buttonPins,  sizeof(cfg.buttonPins));
  prefs.getBytes("encPins", cfg.encoderPins, sizeof(cfg.encoderPins));
  cfg.useMatrix        = prefs.getBool("useMat",       cfg.useMatrix);
  cfg.matrixDirectMode = prefs.getBool("matDirect",    cfg.matrixDirectMode);
  cfg.matrixRows       = prefs.getUChar("matRows",     cfg.matrixRows);
  cfg.matrixCols       = prefs.getUChar("matCols",     cfg.matrixCols);
  prefs.getBytes("matRowPins", cfg.matrixRowPins, sizeof(cfg.matrixRowPins));
  prefs.getBytes("matColPins", cfg.matrixColPins, sizeof(cfg.matrixColPins));
  cfg.recoveryOccurred = prefs.getBool("recovered", false);
  prefs.end();
  return cfg;
}

inline void saveConfig(const Config& cfg) {
  Preferences prefs;
  prefs.begin("buttonbox", false);
  prefs.putString("bleName",      cfg.bleDeviceName);
  prefs.putUChar("numButtons",   cfg.numButtons);
  prefs.putString("otaPassword", cfg.otaPassword);
  prefs.putBool("useEncoders",    cfg.useEncoders);
  prefs.putUInt("debounceMs",     cfg.debounceDelayMs);
  prefs.putUInt("encDebounceUs",  cfg.encoderDebounceUs);
  prefs.putUInt("btnTaskMs",      cfg.buttonTaskDelayMs);
  prefs.putUInt("encPressMs",     cfg.encoderPressDurationMs);
  prefs.putUInt("encTaskMs",      cfg.encoderTaskDelayMs);
  prefs.putBool("encZonesMode",   cfg.encoderZonesMode);
  prefs.putUInt("encZoneMaster",  cfg.encoderZoneMaster);
  prefs.putUInt("encZoneSteps",   cfg.encoderZoneSteps);
  prefs.putUInt("encZoneCount",   cfg.encoderZoneCount);
  prefs.putUInt("encResetMask",   cfg.encoderZoneResetMask);
  prefs.putBytes("btnPins",    cfg.buttonPins,     32);
  prefs.putBytes("encPins",    cfg.encoderPins,    sizeof(cfg.encoderPins));
  prefs.putBool("useMat",       cfg.useMatrix);
  prefs.putBool("matDirect",    cfg.matrixDirectMode);
  prefs.putUChar("matRows",     cfg.matrixRows);
  prefs.putUChar("matCols",     cfg.matrixCols);
  prefs.putBytes("matRowPins", cfg.matrixRowPins,  sizeof(cfg.matrixRowPins));
  prefs.putBytes("matColPins", cfg.matrixColPins,  sizeof(cfg.matrixColPins));
  prefs.putBool("recovered",   cfg.recoveryOccurred);
  prefs.end();
}

inline String configToJson(const Config& cfg) {
  // static: allocated once on BSS, not the stack — avoids stack overflow
  // in the NimBLE host task's onRead callback.
  static StaticJsonDocument<2048> doc;
  doc.clear();
  doc["bleDeviceName"]          = cfg.bleDeviceName;
  doc["numButtons"]             = cfg.numButtons;
  doc["otaPassword"]            = cfg.otaPassword;
  doc["useEncoders"]            = cfg.useEncoders;
  doc["debounceDelayMs"]        = cfg.debounceDelayMs;
  doc["encoderDebounceUs"]      = cfg.encoderDebounceUs;
  doc["buttonTaskDelayMs"]      = cfg.buttonTaskDelayMs;
  doc["encoderPressDurationMs"] = cfg.encoderPressDurationMs;
  doc["encoderTaskDelayMs"]     = cfg.encoderTaskDelayMs;
  doc["encoderZonesMode"]       = cfg.encoderZonesMode;
  doc["encoderZoneMaster"]      = cfg.encoderZoneMaster;
  doc["encoderZoneSteps"]       = cfg.encoderZoneSteps;
  doc["encoderZoneCount"]       = cfg.encoderZoneCount;
  JsonArray resetArr = doc.createNestedArray("encoderZoneResetButtons");
  for (int i = 0; i < (int)cfg.numButtons; i++)
    if (cfg.encoderZoneResetMask & (1u << i)) resetArr.add(i + 1);
  JsonArray bpArr = doc.createNestedArray("buttonPins");
  for (int i = 0; i < (int)cfg.numButtons; i++) bpArr.add(cfg.buttonPins[i]);
  JsonArray epArr = doc.createNestedArray("encoderPins");
  for (int i = 0; i < 2; i++) {
    JsonArray row = epArr.createNestedArray();
    row.add(cfg.encoderPins[i][0]);
    row.add(cfg.encoderPins[i][1]);
  }
  doc["useMatrix"]        = cfg.useMatrix;
  doc["matrixDirectMode"] = cfg.matrixDirectMode;
  doc["matrixRows"]       = cfg.matrixRows;
  doc["matrixCols"]       = cfg.matrixCols;
  JsonArray mrArr = doc.createNestedArray("matrixRowPins");
  for (int i = 0; i < (int)cfg.matrixRows; i++) mrArr.add(cfg.matrixRowPins[i]);
  JsonArray mcArr = doc.createNestedArray("matrixColPins");
  for (int i = 0; i < (int)cfg.matrixCols; i++) mcArr.add(cfg.matrixColPins[i]);
  doc["recoveryOccurred"] = cfg.recoveryOccurred;
  String out;
  serializeJson(doc, out);
  return out;
}

inline bool jsonToConfig(const String& json, Config& cfg) {
  static StaticJsonDocument<2048> doc;
  doc.clear();
  if (deserializeJson(doc, json)) return false;
  if (doc.containsKey("bleDeviceName"))          cfg.bleDeviceName          = doc["bleDeviceName"].as<String>();
  if (doc.containsKey("numButtons"))             cfg.numButtons             = constrain(doc["numButtons"].as<int>(), 0, 32);
  if (doc.containsKey("otaPassword"))            cfg.otaPassword            = doc["otaPassword"].as<String>();
  if (doc.containsKey("useEncoders"))            cfg.useEncoders            = doc["useEncoders"].as<bool>();
  if (doc.containsKey("debounceDelayMs"))        cfg.debounceDelayMs        = doc["debounceDelayMs"].as<uint32_t>();
  if (doc.containsKey("encoderDebounceUs"))      cfg.encoderDebounceUs      = doc["encoderDebounceUs"].as<uint32_t>();
  if (doc.containsKey("buttonTaskDelayMs"))      cfg.buttonTaskDelayMs      = doc["buttonTaskDelayMs"].as<uint32_t>();
  if (doc.containsKey("encoderPressDurationMs")) cfg.encoderPressDurationMs = doc["encoderPressDurationMs"].as<uint32_t>();
  if (doc.containsKey("encoderTaskDelayMs"))     cfg.encoderTaskDelayMs     = doc["encoderTaskDelayMs"].as<uint32_t>();
  if (doc.containsKey("encoderZonesMode"))       cfg.encoderZonesMode       = doc["encoderZonesMode"].as<bool>();
  if (doc.containsKey("encoderZoneMaster"))      cfg.encoderZoneMaster      = doc["encoderZoneMaster"].as<uint32_t>();
  if (doc.containsKey("encoderZoneSteps"))       cfg.encoderZoneSteps       = doc["encoderZoneSteps"].as<uint32_t>();
  if (doc.containsKey("encoderZoneCount"))       cfg.encoderZoneCount       = doc["encoderZoneCount"].as<uint32_t>();
  if (doc.containsKey("encoderZoneResetButtons")) {
    cfg.encoderZoneResetMask = 0;
    for (int btn : doc["encoderZoneResetButtons"].as<JsonArray>())
      if (btn >= 1 && btn <= (int)cfg.numButtons) cfg.encoderZoneResetMask |= (1u << (btn - 1));
  }
  if (doc.containsKey("useMatrix"))        cfg.useMatrix        = doc["useMatrix"].as<bool>();
  if (doc.containsKey("matrixDirectMode")) cfg.matrixDirectMode = doc["matrixDirectMode"].as<bool>();
  if (doc.containsKey("matrixRows"))       cfg.matrixRows       = constrain(doc["matrixRows"].as<int>(), 1, 8);
  if (doc.containsKey("matrixCols"))       cfg.matrixCols       = constrain(doc["matrixCols"].as<int>(), 1, 8);
  if (doc.containsKey("matrixRowPins")) {
    JsonArrayConst mr = doc["matrixRowPins"].as<JsonArrayConst>();
    for (int i = 0; i < 8 && i < (int)mr.size(); i++) cfg.matrixRowPins[i] = mr[i].as<uint8_t>();
  }
  if (doc.containsKey("matrixColPins")) {
    JsonArrayConst mc = doc["matrixColPins"].as<JsonArrayConst>();
    for (int i = 0; i < 8 && i < (int)mc.size(); i++) cfg.matrixColPins[i] = mc[i].as<uint8_t>();
  }
  if (doc.containsKey("buttonPins")) {
    JsonArrayConst bp = doc["buttonPins"].as<JsonArrayConst>();
    for (int i = 0; i < 32 && i < (int)bp.size(); i++)
      cfg.buttonPins[i] = bp[i].as<uint8_t>();
  }
  if (doc.containsKey("encoderPins")) {
    JsonArrayConst ep = doc["encoderPins"].as<JsonArrayConst>();
    for (int i = 0; i < 2 && i < (int)ep.size(); i++) {
      JsonArrayConst row = ep[i].as<JsonArrayConst>();
      for (int j = 0; j < 2 && j < (int)row.size(); j++)
        cfg.encoderPins[i][j] = row[j].as<uint8_t>();
    }
  }
  return true;
}
