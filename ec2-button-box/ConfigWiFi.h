#pragma once
// WiFi AP + HTTP Configuration Mode
// No extra libraries needed — WiFi.h and WebServer.h are part of the ESP32 Arduino core.
//
// Usage:
//   1. Hold CONFIG_BOOT_PIN (button 1, pin 2) while powering on.
//   2. Connect your phone/PC to the WiFi network "ButtonBox-Config".
//   3. Open http://192.168.4.1 in any browser.
//   4. Edit settings and press Save. The device reboots into gamepad mode.

#include <WiFi.h>
#include <WebServer.h>
#include "Config.h"

static WebServer _cfgServer(80);

// ── Embedded config page ─────────────────────────────────────────────────────
// Served at http://192.168.4.1/ when in config mode.
static const char _CFG_HTML[] PROGMEM = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8"/>
  <meta name="viewport" content="width=device-width,initial-scale=1.0"/>
  <title>ButtonBox Config</title>
  <style>
    *,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
    :root{
      --bg:#0f1117;--surface:#1a1d27;--border:#2e3347;
      --accent:#4f8ef7;--accent2:#3a6fd4;--danger:#e05555;
      --success:#4caf7d;--text:#e2e6f0;--muted:#7b829a;--r:8px;
    }
    body{background:var(--bg);color:var(--text);font-family:'Segoe UI',system-ui,sans-serif;
         min-height:100vh;display:flex;flex-direction:column;align-items:center;padding:2rem 1rem}
    header{text-align:center;margin-bottom:2rem}
    header h1{font-size:1.6rem;font-weight:700}
    header p{color:var(--muted);margin-top:.3rem;font-size:.9rem}
    .card{background:var(--surface);border:1px solid var(--border);border-radius:var(--r);
          padding:1.5rem;width:100%;max-width:480px}
    .card+.card{margin-top:1rem}
    .card h2{font-size:.85rem;font-weight:600;color:var(--muted);text-transform:uppercase;
             letter-spacing:.8px;margin-bottom:1.1rem}
    .field{display:flex;align-items:center;justify-content:space-between;
           padding:.55rem 0;border-bottom:1px solid var(--border)}
    .field:last-child{border-bottom:none}
    .field label{display:flex;flex-direction:column;gap:2px}
    .field label span{font-size:.95rem}
    .field label small{color:var(--muted);font-size:.75rem}
    input[type="number"]{width:90px;background:var(--bg);border:1px solid var(--border);
      border-radius:6px;color:var(--text);padding:.4rem .55rem;font-size:.95rem;
      text-align:right;outline:none;transition:border-color .15s}
    input[type="number"]:focus{border-color:var(--accent)}
    .toggle{position:relative;width:46px;height:26px}
    .toggle input{opacity:0;width:0;height:0}
    .slider{position:absolute;inset:0;background:var(--border);border-radius:26px;
            cursor:pointer;transition:background .2s}
    .slider::before{content:'';position:absolute;width:20px;height:20px;left:3px;top:3px;
                    background:#fff;border-radius:50%;transition:transform .2s}
    .toggle input:checked+.slider{background:var(--accent)}
    .toggle input:checked+.slider::before{transform:translateX(20px)}
    .btn{display:inline-flex;align-items:center;gap:.4rem;padding:.6rem 1.2rem;
         border-radius:var(--r);font-size:.9rem;font-weight:600;cursor:pointer;border:none;
         transition:opacity .15s,background .15s}
    .btn:disabled{opacity:.45;cursor:not-allowed}
    .btn-primary{background:var(--accent);color:#fff}
    .btn-primary:not(:disabled):hover{background:var(--accent2)}
    .btn-ghost{background:transparent;border:1px solid var(--border);color:var(--text)}
    .btn-ghost:not(:disabled):hover{border-color:var(--accent);color:var(--accent)}
    .actions{display:flex;gap:.75rem;margin-top:1.25rem;flex-wrap:wrap}
    #status{margin-top:.9rem;padding:.6rem .9rem;border-radius:var(--r);
            font-size:.88rem;display:none}
    #status.info{background:#1e2d4a;color:var(--accent);display:block}
    #status.ok{background:#1a3028;color:var(--success);display:block}
    #status.error{background:#3a1a1a;color:var(--danger);display:block}
    footer{margin-top:2.5rem;color:var(--muted);font-size:.78rem;text-align:center}
    .mode-select{display:flex;gap:.4rem}
    .mode-btn{flex:1;padding:.4rem;border-radius:6px;border:1px solid var(--border);
              background:var(--bg);color:var(--muted);font-size:.85rem;cursor:pointer;
              text-align:center;transition:all .15s}
    .mode-btn.active{border-color:var(--accent);color:var(--accent);background:#1e2d4a}
    .hidden{display:none!important}
  </style>
</head>
<body>
<header>
  <h1>ButtonBox Config</h1>
  <p>ESP32 Steering Wheel &mdash; Wi-Fi Configuration</p>
</header>

<div class="card">
  <h2>Buttons</h2>
  <div class="field">
    <label><span>Button Debounce</span><small>Bounce2 interval (ms)</small></label>
    <input type="number" id="debounceDelayMs" min="0" max="100" value="5"/>
  </div>
  <div class="field">
    <label><span>Button Task Interval</span><small>Button polling delay (ms)</small></label>
    <input type="number" id="buttonTaskDelayMs" min="1" max="500" value="5"/>
  </div>
</div>

<div class="card">
  <h2>Encoders</h2>
  <div class="field">
    <label><span>Use Encoders</span><small>Enable / disable rotary encoders</small></label>
    <label class="toggle"><input type="checkbox" id="useEncoders" onchange="onEncoderToggle()"/><span class="slider"></span></label>
  </div>

  <div id="encoderOptions">
    <div class="field">
      <label><span>Encoder Mode</span><small>Select how encoders behave</small></label>
      <div class="mode-select">
        <div class="mode-btn active" id="modeNormal" onclick="setMode(false)">Normal</div>
        <div class="mode-btn"        id="modeZones"  onclick="setMode(true)">Zones</div>
      </div>
    </div>
    <input type="hidden" id="encoderZonesMode" value="false"/>

    <div id="zonesFields">
      <div class="field">
        <label><span>Master Encoder</span><small>Selector encoder that sets the active zone</small></label>
        <div class="mode-select">
          <div class="mode-btn active" id="masterEnc0" onclick="setMaster(0)">Encoder 1</div>
          <div class="mode-btn"        id="masterEnc1" onclick="setMaster(1)">Encoder 2</div>
        </div>
      </div>
      <input type="hidden" id="encoderZoneMaster" value="0"/>
      <div class="field">
        <label><span>Zone Steps</span><small>Total steps of the master encoder</small></label>
        <input type="number" id="encoderZoneSteps" min="2" max="200" value="20" onchange="updateZoneCountOptions()"/>
      </div>
      <div class="field">
        <label><span>Zone Count</span><small>Only divisors of Zone Steps are valid</small></label>
        <select id="encoderZoneCount" style="width:90px;background:var(--bg);border:1px solid var(--border);border-radius:6px;color:var(--text);padding:.4rem .55rem;font-size:.95rem;outline:none"></select>
      </div>
    </div>

    <div class="field">
      <label><span>Encoder Debounce</span><small>Hardware encoder filter (&micro;s)</small></label>
      <input type="number" id="encoderDebounceUs" min="0" max="50000" value="1000"/>
    </div>
    <div class="field">
      <label><span>Encoder Press Duration</span><small>Simulated key-press length (ms)</small></label>
      <input type="number" id="encoderPressDurationMs" min="10" max="1000" value="100"/>
    </div>
    <div class="field">
      <label><span>Encoder Task Interval</span><small>Encoder polling delay (ms)</small></label>
      <input type="number" id="encoderTaskDelayMs" min="1" max="500" value="5"/>
    </div>
  </div>

  <div class="actions">
    <button class="btn btn-primary" id="btnSave" onclick="saveConfig()">Save &amp; Reboot</button>
    <button class="btn btn-ghost"   onclick="loadConfig()">Refresh</button>
  </div>
  <div id="status"></div>
</div>

<footer>Connected via Wi-Fi &mdash; ButtonBox-Config &nbsp;&bull;&nbsp; http://192.168.4.1</footer>

<script>
  const FIELDS = ['useEncoders','debounceDelayMs','encoderDebounceUs',
                  'buttonTaskDelayMs','encoderPressDurationMs','encoderTaskDelayMs',
                  'encoderZoneSteps','encoderZoneCount'];


  function setStatus(msg, type) {
    const el = document.getElementById('status');
    el.className = type; el.textContent = msg;
  }

  function onEncoderToggle() {
    const on = document.getElementById('useEncoders').checked;
    document.getElementById('encoderOptions').classList.toggle('hidden', !on);
  }

  function updateZoneCountOptions() {
    const steps = Number(document.getElementById('encoderZoneSteps').value) || 20;
    const select = document.getElementById('encoderZoneCount');
    const prev = Number(select.value);
    select.innerHTML = '';
    for (let i = 2; i <= steps; i++) {
      if (steps % i === 0) {
        const opt = document.createElement('option');
        opt.value = i; opt.textContent = i;
        select.appendChild(opt);
      }
    }
    // keep previous value if still valid, otherwise pick first option
    select.value = (steps % prev === 0 && prev >= 2) ? prev : select.options[0].value;
  }

  function setMaster(idx) {
    document.getElementById('encoderZoneMaster').value = idx;
    document.getElementById('masterEnc0').classList.toggle('active', idx === 0);
    document.getElementById('masterEnc1').classList.toggle('active', idx === 1);
  }

  function setMode(zones) {
    document.getElementById('encoderZonesMode').value = zones ? 'true' : 'false';
    document.getElementById('modeNormal').classList.toggle('active', !zones);
    document.getElementById('modeZones').classList.toggle('active',  zones);
    document.getElementById('zonesFields').classList.toggle('hidden', !zones);
  }

  function populateForm(cfg) {
    for (const k of FIELDS) {
      if (k === 'encoderZoneCount') continue; // handled separately below
      if (cfg[k] === undefined) continue;
      const el = document.getElementById(k);
      if (!el) continue;
      el.type === 'checkbox' ? (el.checked = cfg[k]) : (el.value = cfg[k]);
    }
    updateZoneCountOptions();
    if (cfg.encoderZoneCount !== undefined)
      document.getElementById('encoderZoneCount').value = cfg.encoderZoneCount;
    if (cfg.encoderZonesMode  !== undefined) setMode(cfg.encoderZonesMode);
    if (cfg.encoderZoneMaster !== undefined) setMaster(cfg.encoderZoneMaster);
    onEncoderToggle();
  }

  function readForm() {
    const cfg = {};
    for (const k of FIELDS) {
      const el = document.getElementById(k);
      if (!el) continue;
      cfg[k] = el.type === 'checkbox' ? el.checked : Number(el.value);
    }
    cfg.encoderZonesMode  = document.getElementById('encoderZonesMode').value === 'true';
    cfg.encoderZoneMaster = Number(document.getElementById('encoderZoneMaster').value);
    return cfg;
  }

  async function loadConfig() {
    try {
      setStatus('Loading...', 'info');
      const res = await fetch('/config');
      populateForm(await res.json());
      setStatus('Config loaded.', 'ok');
    } catch (e) { setStatus('Failed to load: ' + e.message, 'error'); }
  }

  async function saveConfig() {
    const btn = document.getElementById('btnSave');
    btn.disabled = true;
    try {
      setStatus('Saving...', 'info');
      const res = await fetch('/config', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(readForm())
      });
      if (res.ok) {
        setStatus('Saved! Device is rebooting. Reconnect to your normal Wi-Fi.', 'ok');
      } else {
        throw new Error('HTTP ' + res.status);
      }
    } catch (e) {
      btn.disabled = false;
      setStatus('Save failed: ' + e.message, 'error');
    }
  }

  setMode(false);
  updateZoneCountOptions();
  loadConfig();
</script>
</body>
</html>
)rawhtml";

// ── HTTP handlers ─────────────────────────────────────────────────────────────
static void _handleRoot() {
  _cfgServer.send_P(200, "text/html", _CFG_HTML);
}

static void _handleGetConfig() {
  _cfgServer.send(200, "application/json", configToJson(loadConfig()));
}

static void _handlePostConfig() {
  if (!_cfgServer.hasArg("plain")) {
    _cfgServer.send(400, "application/json", "{\"error\":\"no body\"}");
    return;
  }
  Config cfg = loadConfig();
  if (jsonToConfig(_cfgServer.arg("plain"), cfg)) {
    if (cfg.encoderZonesMode && cfg.encoderZoneSteps % cfg.encoderZoneCount != 0) {
      _cfgServer.send(400, "application/json", "{\"error\":\"zoneCount must be a divisor of zoneSteps\"}");
      return;
    }
    saveConfig(cfg);
    _cfgServer.send(200, "application/json", "{\"ok\":true}");
    #ifdef SERIAL_DEBUG
      Serial.println("[Config] Saved to NVS. Rebooting in 1s...");
    #endif
    delay(1000);
    esp_restart();
  } else {
    _cfgServer.send(400, "application/json", "{\"error\":\"invalid json\"}");
  }
}

// Starts WiFi AP + HTTP server. Never returns; the device reboots after save.
inline void startConfigMode() {
  #ifdef SERIAL_DEBUG
    Serial.begin(115200);
    Serial.println("[Config] Starting WiFi AP 'ButtonBox-Config'...");
  #endif
  WiFi.softAP("ButtonBox-Config");
  #ifdef SERIAL_DEBUG
    Serial.print("[Config] IP: ");
    Serial.println(WiFi.softAPIP());
  #endif

  _cfgServer.on("/",       HTTP_GET,  _handleRoot);
  _cfgServer.on("/config", HTTP_GET,  _handleGetConfig);
  _cfgServer.on("/config", HTTP_POST, _handlePostConfig);
  _cfgServer.begin();

  #ifdef SERIAL_DEBUG
    Serial.println("[Config] Open http://192.168.4.1 in any browser.");
  #endif

  while (true) {
    _cfgServer.handleClient();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}
