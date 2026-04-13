function dbg(label, obj) {
  const panel = document.getElementById('debugLog');
  if (!panel) return;
  const ts  = new Date().toTimeString().slice(0, 8);
  const row = document.createElement('div');
  row.style.cssText = 'padding:.2rem .3rem;border-radius:3px;background:#1a1d27;border-left:2px solid var(--accent);margin-bottom:.1rem';
  row.innerHTML = `<span style="color:var(--muted)">${ts}</span> <strong style="color:var(--accent)">${label}</strong><pre style="margin:0;white-space:pre-wrap;word-break:break-all;color:var(--text);font-size:.78rem">${JSON.stringify(obj, null, 2)}</pre>`;
  panel.appendChild(row);
  while (panel.children.length > 30) panel.removeChild(panel.firstChild);
  panel.scrollTop = panel.scrollHeight;
}

const FIELDS = ['bleDeviceName','otaPassword','useEncoders','debounceDelayMs',
                'encoderDebounceUs','buttonTaskDelayMs','encoderPressDurationMs',
                'encoderTaskDelayMs','encoderZoneSteps','encoderZoneCount',
                'useMatrix','matrixDirectMode','matrixRows','matrixCols'];

// ── Connection state ────────────────────────────────────────────────────────

let _pollTimer      = null;
let _reconnectTimer = null;
let _encodersOn     = true;

function startPoll() {
  stopPoll();
  _pollTimer = setInterval(async () => {
    try {
      const data = await window.go.main.App.GetStatus();
      if (!data.connected) handleDisconnect();
    } catch (_) {}
  }, 4000);
}

function stopPoll() {
  if (_pollTimer) { clearInterval(_pollTimer); _pollTimer = null; }
}

function stopReconnect() {
  if (_reconnectTimer) { clearTimeout(_reconnectTimer); _reconnectTimer = null; }
}

function handleDisconnect() {
  stopPoll();
  stopReconnect();
  stopDebugStream();
  hide('configSection');
  hide('stateFound');
  hide('stateSearching');
  hide('stateNotFound');
  show('stateReconnecting');
  document.getElementById('btnSave').disabled = true;
  document.getElementById('btnOta').disabled  = true;
  scheduleReconnect();
}

function scheduleReconnect() {
  _reconnectTimer = setTimeout(async () => {
    try {
      const data = await window.go.main.App.Discover();
      if (data.found) {
        onConnected();
      } else {
        scheduleReconnect();
      }
    } catch (_) {
      scheduleReconnect();
    }
  }, 5000);
}

function onConnected() {
  stopReconnect();
  document.getElementById('foundBadge').textContent = 'ButtonBox';
  hide('stateSearching'); hide('stateReconnecting'); hide('stateNotFound');
  show('stateFound');
  show('configSection');
  document.getElementById('btnSave').disabled = false;
  document.getElementById('btnOta').disabled  = false;
  _encodersOn = true;
  updateEncToggleBtn();
  loadConfig();
  startPoll();
  const debugCard = document.getElementById('debugCard');
  if (debugCard && !debugCard.classList.contains('hidden')) startDebugStream();
}

function updateEncToggleBtn() {
  const btn = document.getElementById('btnEncToggle');
  if (!btn) return;
  btn.textContent = _encodersOn ? 'Enc ON' : 'Enc OFF';
  btn.style.color = _encodersOn ? '' : 'var(--danger)';
}

// ── Discovery ──────────────────────────────────────────────────────────────

async function startDiscover() {
  stopPoll();
  stopReconnect();
  hide('stateFound'); hide('stateNotFound'); hide('stateReconnecting');
  show('stateSearching');
  try {
    const data = await window.go.main.App.Discover();
    if (data.found) {
      hide('stateSearching');
      onConnected();
    } else {
      hide('stateSearching'); show('stateNotFound');
    }
  } catch (_) {
    hide('stateSearching'); show('stateNotFound');
  }
}

function show(id) { document.getElementById(id).classList.remove('hidden'); }
function hide(id) { document.getElementById(id).classList.add('hidden'); }

// ── Config form ────────────────────────────────────────────────────────────

function setStatus(msg, type) {
  const el = document.getElementById('status');
  el.className = type; el.textContent = msg;
}

function onEncoderToggle() {
  const on = document.getElementById('useEncoders').checked;
  document.getElementById('encoderOptions').classList.toggle('hidden', !on);
}

function buildResetPicker() {
  const picker = document.getElementById('resetBtnPicker');
  picker.innerHTML = '';
  for (let i = 1; i <= 14; i++) {
    const chip = document.createElement('div');
    chip.id = 'resetBtn' + i;
    chip.textContent = i;
    chip.dataset.active = 'false';
    chip.style.cssText = 'width:34px;height:34px;display:flex;align-items:center;justify-content:center;border-radius:6px;border:1px solid var(--border);background:var(--bg);color:var(--muted);cursor:pointer;font-size:.85rem;font-weight:600;transition:all .15s;user-select:none';
    chip.onclick = () => toggleResetBtn(i);
    picker.appendChild(chip);
  }
}

function toggleResetBtn(n) {
  const chip = document.getElementById('resetBtn' + n);
  const active = chip.dataset.active !== 'true';
  chip.dataset.active = active;
  chip.style.borderColor = active ? 'var(--accent)' : 'var(--border)';
  chip.style.color       = active ? 'var(--accent)' : 'var(--muted)';
  chip.style.background  = active ? '#1e2d4a'       : 'var(--bg)';
}

function getResetButtons() {
  const result = [];
  for (let i = 1; i <= 14; i++) {
    const chip = document.getElementById('resetBtn' + i);
    if (chip && chip.dataset.active === 'true') result.push(i);
  }
  return result;
}

function setResetButtons(arr) {
  for (let i = 1; i <= 14; i++) {
    const chip = document.getElementById('resetBtn' + i);
    if (!chip) continue;
    const active = arr.includes(i);
    chip.dataset.active = active;
    chip.style.borderColor = active ? 'var(--accent)' : 'var(--border)';
    chip.style.color       = active ? 'var(--accent)' : 'var(--muted)';
    chip.style.background  = active ? '#1e2d4a'       : 'var(--bg)';
  }
}

function buildEncTogglePicker(count) {
  count = count || Number(document.getElementById('numButtons').value) || 14;
  const current = getEncToggleButton();
  const picker = document.getElementById('encTogglePicker');
  picker.innerHTML = '';
  for (let i = 1; i <= count; i++) {
    const chip = document.createElement('div');
    chip.id = 'encToggleBtn' + i;
    chip.textContent = i;
    chip.dataset.active = 'false';
    chip.style.cssText = 'width:34px;height:34px;display:flex;align-items:center;justify-content:center;border-radius:6px;border:1px solid var(--border);background:var(--bg);color:var(--muted);cursor:pointer;font-size:.85rem;font-weight:600;transition:all .15s;user-select:none';
    chip.onclick = () => selectEncToggleBtn(i);
    picker.appendChild(chip);
  }
  if (current >= 1 && current <= count) _applyEncToggleChip(current, true);
}

function _applyEncToggleChip(n, active) {
  const chip = document.getElementById('encToggleBtn' + n);
  if (!chip) return;
  chip.dataset.active = active;
  chip.style.borderColor = active ? 'var(--success)' : 'var(--border)';
  chip.style.color       = active ? 'var(--success)' : 'var(--muted)';
  chip.style.background  = active ? '#1a3028'        : 'var(--bg)';
}

function selectEncToggleBtn(n) {
  const prev = getEncToggleButton();
  if (prev) _applyEncToggleChip(prev, false);
  if (prev === n) return;
  _applyEncToggleChip(n, true);
}

function getEncToggleButton() {
  const picker = document.getElementById('encTogglePicker');
  if (!picker) return 0;
  for (const chip of picker.children)
    if (chip.dataset.active === 'true') return Number(chip.textContent);
  return 0;
}

function setEncToggleButton(n) {
  buildEncTogglePicker();
  if (n >= 1) _applyEncToggleChip(n, true);
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
  _loadedCfg = cfg;
  document.getElementById('recoveryBanner').classList.toggle('visible', !!cfg.recoveryOccurred);
  for (const k of FIELDS) {
    if (k === 'encoderZoneCount') continue;
    if (cfg[k] === undefined) continue;
    const el = document.getElementById(k);
    if (!el) continue;
    el.type === 'checkbox' ? (el.checked = cfg[k]) : (el.value = cfg[k]);
  }
  const rawN = cfg.numButtons != null ? cfg.numButtons : 14;
  const useDirectBtns = rawN > 0;
  document.getElementById('useDirectButtons').checked = useDirectBtns;
  onDirectBtnsToggle();
  const n = useDirectBtns ? rawN : 14;
  if (useDirectBtns) document.getElementById('numButtons').value = n;
  buildBtnPinGrid(useDirectBtns ? n : 0);
  updateZoneCountOptions();
  if (cfg.encoderZoneCount       !== undefined) document.getElementById('encoderZoneCount').value = cfg.encoderZoneCount;
  if (cfg.encoderZonesMode       !== undefined) setMode(cfg.encoderZonesMode);
  if (cfg.encoderZoneMaster      !== undefined) setMaster(cfg.encoderZoneMaster);
  if (cfg.encoderZoneResetButtons !== undefined) setResetButtons(cfg.encoderZoneResetButtons);
  if (cfg.encoderToggleButton    !== undefined) setEncToggleButton(cfg.encoderToggleButton);
  if (cfg.buttonPins !== undefined)
    cfg.buttonPins.forEach((pin, i) => {
      const el = document.getElementById('btnPin' + (i + 1));
      if (el) el.value = pin;
    });
  if (cfg.buttonInputModes !== undefined)
    cfg.buttonInputModes.forEach((mode, i) => {
      const el = document.getElementById('btnMode' + (i + 1));
      if (el) el.value = mode;
    });
  if (cfg.encoderPins !== undefined) {
    const ids = [['encPin0clk',0,0],['encPin0dt',0,1],['encPin1clk',1,0],['encPin1dt',1,1]];
    ids.forEach(([id, enc, ch]) => {
      const el = document.getElementById(id);
      if (el) el.value = cfg.encoderPins[enc][ch];
    });
  }
  onMatrixToggle();
  onMatrixDirectModeToggle();
  if (cfg.matrixRows !== undefined || cfg.matrixCols !== undefined) {
    buildMatrixPinGrid('matRowPinGrid', cfg.matrixRows || 4, 'R');
    buildMatrixPinGrid('matColPinGrid', cfg.matrixCols || 4, 'C');
  }
  if (cfg.matrixRowPins) cfg.matrixRowPins.forEach((p, i) => {
    const el = document.getElementById('matRowPinGrid_' + (i + 1));
    if (el) el.value = p;
  });
  if (cfg.matrixColPins) cfg.matrixColPins.forEach((p, i) => {
    const el = document.getElementById('matColPinGrid_' + (i + 1));
    if (el) el.value = p;
  });
  onEncoderToggle();
}

function readForm() {
  const cfg = {};
  const btnCount = document.getElementById('useDirectButtons').checked
    ? Math.max(1, Number(document.getElementById('numButtons').value))
    : 0;
  cfg.numButtons = btnCount;
  for (const k of FIELDS) {
    const el = document.getElementById(k);
    if (!el) continue;
    cfg[k] = el.type === 'checkbox' ? el.checked : el.type === 'text' ? el.value : Number(el.value);
  }
  cfg.encoderZonesMode        = document.getElementById('encoderZonesMode').value === 'true';
  cfg.encoderZoneMaster       = Number(document.getElementById('encoderZoneMaster').value);
  cfg.encoderZoneResetButtons = getResetButtons();
  cfg.encoderToggleButton     = getEncToggleButton();
  cfg.buttonPins = Array.from({length: btnCount}, (_, i) =>
    Number(document.getElementById('btnPin' + (i + 1))?.value ?? DEFAULT_BTN_PINS[i] ?? 0));
  cfg.buttonInputModes = Array.from({length: btnCount}, (_, i) =>
    Number(document.getElementById('btnMode' + (i + 1))?.value ?? 0));
  cfg.encoderPins = [
    [Number(document.getElementById('encPin0clk').value), Number(document.getElementById('encPin0dt').value)],
    [Number(document.getElementById('encPin1clk').value), Number(document.getElementById('encPin1dt').value)],
  ];
  const rowCount = Math.max(1, Math.min(8, Number(document.getElementById('matrixRows').value) || 4));
  const colCount = Math.max(1, Math.min(8, Number(document.getElementById('matrixCols').value) || 4));
  cfg.matrixRowPins = Array.from({length: rowCount}, (_, i) =>
    Number(document.getElementById('matRowPinGrid_' + (i + 1))?.value ?? 0));
  cfg.matrixColPins = Array.from({length: colCount}, (_, i) =>
    Number(document.getElementById('matColPinGrid_' + (i + 1))?.value ?? 0));
  return cfg;
}

async function loadConfig() {
  try {
    setStatus('Loading…', 'info');
    const cfgStr = await window.go.main.App.GetConfig();
    const cfg = JSON.parse(cfgStr);
    dbg('RECEIVED from device', cfg);
    populateForm(cfg);
    setStatus('Config loaded.', 'ok');
  } catch (e) {
    const msg = typeof e === 'string' ? e : e.message;
    if (!msg || msg.includes('not connected')) { handleDisconnect(); return; }
    setStatus('Read failed: ' + msg, 'error');
  }
}

async function saveConfig() {
  const btn = document.getElementById('btnSave');
  btn.disabled = true;
  try {
    setStatus('Saving…', 'info');
    const payload = readForm();
    dbg('SENDING to device', payload);
    await window.go.main.App.SaveConfig(JSON.stringify(payload));
    setStatus('Saved! Device is rebooting…', 'ok');
    handleDisconnect();
  } catch (e) {
    btn.disabled = false;
    const msg = typeof e === 'string' ? e : e.message;
    if (!msg || msg.includes('not connected')) { handleDisconnect(); return; }
    setStatus('Save failed: ' + msg, 'error');
  }
}

// ── Matrix pin grids ───────────────────────────────────────────────────────

function onDirectBtnsToggle() {
  const on = document.getElementById('useDirectButtons').checked;
  document.getElementById('directBtnOptions').classList.toggle('hidden', !on);
}

function onMatrixDirectModeToggle() {
  const direct = document.getElementById('matrixDirectMode').checked;
  const hint = document.getElementById('matRowPinsHint');
  if (hint) hint.textContent = direct ? 'GPIO INPUT_PULLUP — each pin is a button' : 'GPIO driven LOW when scanning a row';
}

function onMatrixToggle() {
  const on = document.getElementById('useMatrix').checked;
  document.getElementById('matrixOptions').classList.toggle('hidden', !on);
}

function onMatrixDimsChange() {
  buildMatrixPinGrid('matRowPinGrid', Number(document.getElementById('matrixRows').value), 'R');
  buildMatrixPinGrid('matColPinGrid', Number(document.getElementById('matrixCols').value), 'C');
}

function buildMatrixPinGrid(gridId, count, prefix) {
  count = Math.max(1, Math.min(8, count || 1));
  const saved = {};
  for (let i = 1; i <= 8; i++) {
    const el = document.getElementById(gridId + '_' + i);
    if (el) saved[i] = Number(el.value);
  }
  const grid = document.getElementById(gridId);
  grid.style.gridTemplateColumns = 'repeat(' + count + ', 1fr)';
  grid.innerHTML = '';
  for (let i = 1; i <= count; i++) {
    const cell = document.createElement('div');
    cell.className = 'pin-cell';
    cell.innerHTML = `<span>${prefix}${i}</span><input type="number" id="${gridId}_${i}" min="0" max="39" value="${saved[i] ?? 0}"/>`;
    grid.appendChild(cell);
  }
}

// ── OTA ────────────────────────────────────────────────────────────────────

function setOtaStatus(msg, type) {
  const el = document.getElementById('otaStatus');
  el.style.display = 'block';
  el.style.background = type === 'ok' ? '#1a3028' : type === 'error' ? '#3a1a1a' : '#1e2d4a';
  el.style.color = type === 'ok' ? 'var(--success)' : type === 'error' ? 'var(--danger)' : 'var(--accent)';
  el.innerHTML = msg;
}

async function startOTA() {
  const btn = document.getElementById('btnOta');
  btn.disabled = true;
  setOtaStatus('Switching to OTA mode&hellip;', 'info');
  try {
    await window.go.main.App.TriggerOTA();
    setOtaStatus(
      'OTA mode active!<br><small style="opacity:.8">1. Connect your PC to the <strong>ButtonBox-OTA</strong> WiFi network.<br>2. In Arduino IDE: Tools &rarr; Port &rarr; <strong>buttonbox</strong>.<br>3. Upload your sketch normally.</small>',
      'ok');
    hide('configSection');
    hide('stateFound');
    document.getElementById('stateNotFound').classList.remove('hidden');
    document.getElementById('stateSearching').classList.add('hidden');
  } catch (e) {
    btn.disabled = false;
    const msg = typeof e === 'string' ? e : e.message;
    setOtaStatus('Failed: ' + msg, 'error');
  }
}

async function clearCache() {
  const el = document.getElementById('cacheStatus');
  el.style.display = 'block';
  el.style.background = '#1e2d4a'; el.style.color = 'var(--accent)';
  el.textContent = 'Clearing…';
  try {
    await window.go.main.App.ClearCache();
    el.style.background = '#1a3028'; el.style.color = 'var(--success)';
    el.textContent = 'Cache cleared. Next connect will show device defaults.';
  } catch (e) {
    el.style.background = '#3a1a1a'; el.style.color = 'var(--danger)';
    el.textContent = 'Failed: ' + (typeof e === 'string' ? e : e.message);
  }
}

async function toggleEncoders() {
  const next = !_encodersOn;
  try {
    await window.go.main.App.SetEncoders(next);
    _encodersOn = next;
    updateEncToggleBtn();
  } catch (e) {
    const msg = typeof e === 'string' ? e : e.message;
    setStatus('Encoder toggle failed: ' + msg, 'error');
  }
}

// ── Pin grid ───────────────────────────────────────────────────────────────

const DEFAULT_BTN_PINS = [2, 5, 13, 14, 15, 17, 18, 19, 21, 22, 23, 25, 32, 33];

function buildBtnPinGrid(count) {
  count = (count != null) ? count : Number(document.getElementById('numButtons').value);
  count = Math.max(0, Math.min(32, count));
  const saved = {}, savedModes = {};
  for (let i = 1; i <= 32; i++) {
    const el = document.getElementById('btnPin' + i);
    if (el) saved[i] = Number(el.value);
    const me = document.getElementById('btnMode' + i);
    if (me) savedModes[i] = Number(me.value);
  }
  const grid = document.getElementById('btnPinGrid');
  grid.innerHTML = '';
  for (let i = 1; i <= count; i++) {
    const val  = saved[i]      !== undefined ? saved[i]      : (DEFAULT_BTN_PINS[i-1] ?? 0);
    const mval = savedModes[i] !== undefined ? savedModes[i] : 0;
    const sel = (v, label) => `<option value="${v}"${mval===v?' selected':''}>${label}</option>`;
    const cell = document.createElement('div');
    cell.className = 'pin-cell';
    cell.innerHTML =
      `<span>B${i}</span>` +
      `<input type="number" id="btnPin${i}" min="0" max="39" value="${val}"/>` +
      `<select id="btnMode${i}" title="PU=INPUT_PULLUP  PD=INPUT_PULLDOWN  IN=INPUT (external resistor)">` +
        sel(0,'PU') + sel(1,'PD') + sel(2,'IN') +
      `</select>`;
    grid.appendChild(cell);
  }
}

function onNumButtonsChange() {
  const n = Math.max(0, Math.min(32, Number(document.getElementById('numButtons').value)));
  buildBtnPinGrid(n);
  buildEncTogglePicker(n);
}

// ── Init ───────────────────────────────────────────────────────────────────
buildBtnPinGrid(14);
buildMatrixPinGrid('matRowPinGrid', 4, 'R');
buildMatrixPinGrid('matColPinGrid', 4, 'C');
buildResetPicker();
buildEncTogglePicker(14);
setMode(false);
updateZoneCountOptions();
startDiscover();

// ── Debug monitor ───────────────────────────────────────────────────────────

let _btnEventUnlisten = null;
let _loadedCfg        = null;
const _pressedBtns    = new Set();
const _recentBtns     = [];
const MAX_LOG         = 50;

function getButtonLabel(n) {
  if (!_loadedCfg || !_loadedCfg.useEncoders)
    return { label: String(n), enc: false };
  const numDirect  = _loadedCfg.numButtons  || 0;
  const matButtons = _loadedCfg.useMatrix
    ? (_loadedCfg.matrixRows || 0) * (_loadedCfg.matrixCols || 0) : 0;
  const encBase = numDirect + matButtons + 1;
  if (n < encBase) return { label: String(n), enc: false };
  const idx = n - encBase;
  if (_loadedCfg.encoderZonesMode) {
    const zone = Math.floor(idx / 2);
    const dir  = idx % 2 === 0 ? 'CW' : 'CCW';
    const zoneCount = _loadedCfg.encoderZoneCount || 2;
    if (zone < zoneCount) return { label: `${n} Z${zone + 1} ${dir}`, enc: true };
  } else {
    const encIdx = Math.floor(idx / 2);
    const dir    = idx % 2 === 0 ? 'CW' : 'CCW';
    if (encIdx < 2) return { label: `${n} Enc${encIdx + 1} ${dir}`, enc: true };
  }
  return { label: String(n), enc: false };
}

function toggleDebug() {
  const card = document.getElementById('debugCard');
  const btn  = document.getElementById('btnDebug');
  const on   = card.classList.toggle('hidden');
  btn.style.color = on ? '' : 'var(--accent)';
  if (!on) startDebugStream(); else stopDebugStream();
}

function startDebugStream() {
  if (_btnEventUnlisten) return;
  renderRecent();
  _btnEventUnlisten = window.runtime.EventsOn('btn-event', (data) => {
    const { btn, evt } = data;
    const press = evt === 'press';
    if (press) {
      _pressedBtns.add(btn);
      if (_recentBtns[_recentBtns.length - 1] !== btn) _recentBtns.push(btn);
    } else {
      _pressedBtns.delete(btn);
    }
    renderPressed();
    renderRecent();
    appendLog(btn, press);
  });
}

function stopDebugStream() {
  if (_btnEventUnlisten) { _btnEventUnlisten(); _btnEventUnlisten = null; }
  _pressedBtns.clear();
  _recentBtns.length = 0;
  renderPressed();
  renderRecent();
}

function renderPressed() {
  const el = document.getElementById('debugPressed');
  el.innerHTML = '';
  if (_pressedBtns.size === 0) {
    el.innerHTML = '<span style="color:var(--muted);font-size:.82rem">— none —</span>';
    return;
  }
  [..._pressedBtns].sort((a, b) => a - b).forEach(n => {
    const { label, enc } = getButtonLabel(n);
    const chip = document.createElement('span');
    chip.textContent = label;
    chip.style.cssText = `display:inline-flex;align-items:center;justify-content:center;padding:0 .5rem;min-width:34px;height:34px;border-radius:6px;background:${enc ? 'var(--success)' : 'var(--accent)'};color:#fff;font-size:.8rem;font-weight:700`;
    el.appendChild(chip);
  });
}

function appendLog(btn, press) {
  const log  = document.getElementById('debugLog');
  const time = new Date().toLocaleTimeString('en', {hour12: false, hour:'2-digit', minute:'2-digit', second:'2-digit'});
  const { label, enc } = getButtonLabel(btn);
  const row  = document.createElement('div');
  row.style.cssText = `padding:.15rem .3rem;border-radius:3px;background:${press ? '#1a2d1a' : '#1a1a2d'}`;
  const kind = enc ? 'enc' : 'btn';
  const color = enc ? (press ? '#4caf7d' : '#7b829a') : (press ? 'var(--success)' : 'var(--accent)');
  row.innerHTML = `<span style="color:var(--muted)">${time}</span>&nbsp; ${kind} <strong style="color:${color}">${label}</strong>&nbsp;<span style="color:var(--muted)">${press ? 'pressed' : 'released'}</span>`;
  log.appendChild(row);
  while (log.children.length > MAX_LOG) log.removeChild(log.firstChild);
  log.scrollTop = log.scrollHeight;
}

function renderRecent() {
  const el    = document.getElementById('debugRecent');
  const limit = Math.max(1, Math.min(32, Number(document.getElementById('recentCount').value) || 10));
  const slice = _recentBtns.slice(-limit);
  el.innerHTML = '';
  if (slice.length === 0) {
    el.innerHTML = '<span style="color:var(--muted);font-size:.82rem">— none —</span>';
    return;
  }
  slice.reverse().forEach(n => {
    const { label, enc } = getButtonLabel(n);
    const chip = document.createElement('span');
    chip.textContent = label;
    chip.style.cssText = `display:inline-flex;align-items:center;justify-content:center;padding:0 .5rem;min-width:34px;height:34px;border-radius:6px;background:${enc ? '#1a3028' : '#1a1d3a'};border:1px solid ${enc ? 'var(--success)' : 'var(--accent)'};color:${enc ? 'var(--success)' : 'var(--accent)'};font-size:.8rem;font-weight:700`;
    el.appendChild(chip);
  });
}

function clearLog() {
  document.getElementById('debugLog').innerHTML = '';
  _pressedBtns.clear();
  _recentBtns.length = 0;
  renderPressed();
  renderRecent();
}
