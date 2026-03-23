#pragma once
// Requires: ArduinoJson >= 6.x  (install via Library Manager)
#include <Preferences.h>
#include <ArduinoJson.h>

struct Config {
  bool     useEncoders            = true;
  uint32_t debounceDelayMs        = 5;     // Bounce2 button debounce (ms)
  uint32_t encoderDebounceUs      = 1000;  // Encoder debounce (µs)
  uint32_t buttonTaskDelayMs      = 5;     // buttonTask polling interval (ms)
  uint32_t encoderPressDurationMs = 100;   // Simulated encoder key-press duration (ms)
  uint32_t encoderTaskDelayMs     = 5;     // encoderTask polling interval (ms)
};

inline Config loadConfig() {
  Config cfg;
  Preferences prefs;
  if (!prefs.begin("buttonbox", true)) return cfg;  // returns defaults if no NVS entry
  cfg.useEncoders            = prefs.getBool("useEncoders",  cfg.useEncoders);
  cfg.debounceDelayMs        = prefs.getUInt("debounceMs",   cfg.debounceDelayMs);
  cfg.encoderDebounceUs      = prefs.getUInt("encDebounceUs",cfg.encoderDebounceUs);
  cfg.buttonTaskDelayMs      = prefs.getUInt("btnTaskMs",    cfg.buttonTaskDelayMs);
  cfg.encoderPressDurationMs = prefs.getUInt("encPressMs",   cfg.encoderPressDurationMs);
  cfg.encoderTaskDelayMs     = prefs.getUInt("encTaskMs",    cfg.encoderTaskDelayMs);
  prefs.end();
  return cfg;
}

inline void saveConfig(const Config& cfg) {
  Preferences prefs;
  prefs.begin("buttonbox", false);
  prefs.putBool("useEncoders",  cfg.useEncoders);
  prefs.putUInt("debounceMs",   cfg.debounceDelayMs);
  prefs.putUInt("encDebounceUs",cfg.encoderDebounceUs);
  prefs.putUInt("btnTaskMs",    cfg.buttonTaskDelayMs);
  prefs.putUInt("encPressMs",   cfg.encoderPressDurationMs);
  prefs.putUInt("encTaskMs",    cfg.encoderTaskDelayMs);
  prefs.end();
}

inline String configToJson(const Config& cfg) {
  StaticJsonDocument<256> doc;
  doc["useEncoders"]            = cfg.useEncoders;
  doc["debounceDelayMs"]        = cfg.debounceDelayMs;
  doc["encoderDebounceUs"]      = cfg.encoderDebounceUs;
  doc["buttonTaskDelayMs"]      = cfg.buttonTaskDelayMs;
  doc["encoderPressDurationMs"] = cfg.encoderPressDurationMs;
  doc["encoderTaskDelayMs"]     = cfg.encoderTaskDelayMs;
  String out;
  serializeJson(doc, out);
  return out;
}

inline bool jsonToConfig(const String& json, Config& cfg) {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, json)) return false;
  if (doc.containsKey("useEncoders"))            cfg.useEncoders            = doc["useEncoders"].as<bool>();
  if (doc.containsKey("debounceDelayMs"))        cfg.debounceDelayMs        = doc["debounceDelayMs"].as<uint32_t>();
  if (doc.containsKey("encoderDebounceUs"))      cfg.encoderDebounceUs      = doc["encoderDebounceUs"].as<uint32_t>();
  if (doc.containsKey("buttonTaskDelayMs"))      cfg.buttonTaskDelayMs      = doc["buttonTaskDelayMs"].as<uint32_t>();
  if (doc.containsKey("encoderPressDurationMs")) cfg.encoderPressDurationMs = doc["encoderPressDurationMs"].as<uint32_t>();
  if (doc.containsKey("encoderTaskDelayMs"))     cfg.encoderTaskDelayMs     = doc["encoderTaskDelayMs"].as<uint32_t>();
  return true;
}
