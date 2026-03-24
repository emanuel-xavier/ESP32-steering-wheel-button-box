#pragma once
// WiFi OTA Mode
// Hold the OTA boot button while powering on to enter this mode.
// The ESP32 creates a WiFi AP named "ButtonBox-OTA".
// Connect your PC to that network — the device then appears as a
// network port in Arduino IDE (Tools → Port → buttonbox).
// Select it and upload normally. No USB cable needed.
//
// Built-in ESP32 Arduino core libraries used: WiFi, ArduinoOTA, ESPmDNS

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>

inline void startOTAMode(const String& password) {
  #ifdef SERIAL_DEBUG
    Serial.begin(115200);
    Serial.println("[OTA] Starting WiFi AP 'ButtonBox-OTA'...");
  #endif

  WiFi.softAP("ButtonBox-OTA");

  #ifdef SERIAL_DEBUG
    Serial.print("[OTA] IP: ");
    Serial.println(WiFi.softAPIP());
  #endif

  ArduinoOTA.setHostname("buttonbox");
  if (password.length() > 0) {
    ArduinoOTA.setPassword(password.c_str());
  }

  ArduinoOTA.onStart([]() {
    #ifdef SERIAL_DEBUG
      Serial.println("[OTA] Upload started.");
    #endif
  });

  ArduinoOTA.onEnd([]() {
    #ifdef SERIAL_DEBUG
      Serial.println("[OTA] Upload complete. Rebooting...");
    #endif
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    #ifdef SERIAL_DEBUG
      Serial.printf("[OTA] Progress: %u%%\n", progress * 100 / total);
    #endif
  });

  ArduinoOTA.onError([](ota_error_t error) {
    #ifdef SERIAL_DEBUG
      Serial.printf("[OTA] Error[%u]\n", error);
    #endif
  });

  ArduinoOTA.begin();

  #ifdef SERIAL_DEBUG
    Serial.println("[OTA] Ready. Connect to 'ButtonBox-OTA' WiFi, then upload from Arduino IDE.");
    if (WiFi.softAPIP()) {
      Serial.println("[OTA] Select Tools → Port → buttonbox in Arduino IDE.");
    }
  #endif

  while (true) {
    ArduinoOTA.handle();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}
