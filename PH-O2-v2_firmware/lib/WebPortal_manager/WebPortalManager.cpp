#include "WebPortalManager.h"
#include "services/console/console_service.h"

#include <WiFi.h>

static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="es">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>PH-O2 Portal</title>
  <style>
    :root {
      --bg: #0f172a;
      --panel: #111827;
      --panel-2: #0b1220;
      --text: #e5e7eb;
      --muted: #94a3b8;
      --ok: #22c55e;
      --warn: #f59e0b;
      --bad: #ef4444;
      --accent: #38bdf8;
      --border: #1f2937;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      font-family: "Segoe UI", Tahoma, sans-serif;
      background: radial-gradient(1000px 400px at 10% -10%, #1e293b, transparent), var(--bg);
      color: var(--text);
    }
    header {
      padding: 16px 20px;
      border-bottom: 1px solid var(--border);
      background: linear-gradient(90deg, #0b1220, #111827);
    }
    header h1 {
      margin: 0;
      font-size: 18px;
      letter-spacing: 0.5px;
    }
    main {
      padding: 16px;
      display: grid;
      gap: 16px;
    }
    .grid {
      display: grid;
      gap: 12px;
      grid-template-columns: repeat(auto-fit, minmax(240px, 1fr));
    }
    .card {
      background: var(--panel);
      border: 1px solid var(--border);
      border-radius: 12px;
      padding: 12px;
      min-height: 84px;
    }
    .card h3 {
      margin: 0 0 8px 0;
      font-size: 13px;
      color: var(--muted);
      font-weight: 600;
    }
    .value {
      font-size: 24px;
      font-weight: 700;
    }
    .row {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 8px;
      margin-top: 6px;
      font-size: 13px;
      color: var(--muted);
    }
    .pill {
      padding: 2px 8px;
      border-radius: 999px;
      font-size: 12px;
      background: var(--panel-2);
      border: 1px solid var(--border);
    }
    .led {
      width: 10px;
      height: 10px;
      border-radius: 50%;
      display: inline-block;
      margin-right: 6px;
      background: var(--bad);
      box-shadow: 0 0 6px rgba(239,68,68,0.6);
    }
    .led.ok {
      background: var(--ok);
      box-shadow: 0 0 6px rgba(34,197,94,0.6);
    }
    .btns {
      display: flex;
      flex-wrap: wrap;
      gap: 8px;
    }
    button {
      background: #111827;
      color: var(--text);
      border: 1px solid var(--border);
      border-radius: 10px;
      padding: 8px 12px;
      cursor: pointer;
      font-size: 13px;
    }
    button.primary { border-color: var(--accent); }
    button.warn { border-color: var(--warn); }
    button.bad { border-color: var(--bad); }
    label {
      display: flex;
      flex-direction: column;
      gap: 6px;
      font-size: 12px;
      color: var(--muted);
    }
    input {
      background: #0b1020;
      color: var(--text);
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 6px 8px;
      font-size: 13px;
    }
    input:focus {
      outline: none;
      border-color: var(--accent);
      box-shadow: 0 0 0 2px rgba(34,211,238,0.15);
    }
    .console {
      background: #0b1020;
      border: 1px solid var(--border);
      border-radius: 12px;
      padding: 10px;
      min-height: 160px;
      max-height: 260px;
      overflow: auto;
      font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
      font-size: 12px;
      line-height: 1.4;
      white-space: pre-wrap;
    }
    .muted { color: var(--muted); }
  </style>
</head>
<body>
  <header>
    <h1>PH-O2 Portal</h1>
  </header>
  <main>
    <div class="grid">
      <div class="card">
        <h3>Temp</h3>
        <div class="value" id="tempValue">--</div>
        <div class="row"><span class="muted">Unidades</span><span>C</span></div>
      </div>
      <div class="card">
        <h3>pH</h3>
        <div class="value" id="phValue">--</div>
      </div>
      <div class="card">
        <h3>O2</h3>
        <div class="value" id="o2Value">--</div>
        <div class="row"><span class="muted">Unidades</span><span>mg/L</span></div>
      </div>
      <div class="card">
        <h3>Modo</h3>
        <div class="value" id="autoState">--</div>
        <div class="row"><span class="muted">Ultimo</span><span id="lastResult">--</span></div>
      </div>
      <div class="card">
        <h3>WiFi</h3>
        <div class="value" id="ipValue">--</div>
        <div class="row"><span class="muted">RSSI</span><span id="rssiValue">--</span></div>
      </div>
    </div>

    <div class="grid">
      <div class="card">
        <h3>Niveles</h3>
        <div class="row"><span><span class="led" id="lvlO2"></span>O2</span><span id="lvlO2Text">--</span></div>
        <div class="row"><span><span class="led" id="lvlPH"></span>pH</span><span id="lvlPHText">--</span></div>
        <div class="row"><span><span class="led" id="lvlH2O"></span>H2O</span><span id="lvlH2OText">--</span></div>
        <div class="row"><span><span class="led" id="lvlKCL"></span>KCL</span><span id="lvlKCLText">--</span></div>
      </div>
      <div class="card">
        <h3>Calibracion</h3>
        <div class="row"><span>pH</span><span id="calPhMethod">--</span></div>
        <div class="row"><span class="muted" id="calPhVals">--</span></div>
        <div class="row"><span>O2</span><span id="calO2Method">--</span></div>
        <div class="row"><span class="muted" id="calO2Vals">--</span></div>
      </div>
      <div class="card">
        <h3>Bombas</h3>
        <div class="row"><span><span class="led" id="pKCL"></span>KCL</span><span id="pKCLText">--</span></div>
        <div class="row"><span><span class="led" id="pH2O"></span>H2O</span><span id="pH2OText">--</span></div>
        <div class="row"><span><span class="led" id="pDrain"></span>DRAIN</span><span id="pDrainText">--</span></div>
        <div class="row"><span><span class="led" id="pMixer"></span>MIXER</span><span id="pMixerText">--</span></div>
      </div>
      <div class="card">
        <h3>Acciones</h3>
        <div class="btns">
          <button class="primary" onclick="doAction('auto_start')">Auto start</button>
          <button class="warn" onclick="doAction('auto_cancel')">Auto cancel</button>
          <button onclick="doAction('read_ph')">Leer pH</button>
          <button onclick="doAction('read_o2')">Leer O2</button>
          <button onclick="doAction('read_temp')">Leer temp</button>
          <button class="bad" onclick="doAction('clear_logs')">Limpiar consola</button>
        </div>
        <div class="btns" style="margin-top:10px;">
          <button id="btn-kcl" onclick="togglePump('kcl')">KCL</button>
          <button id="btn-h2o" onclick="togglePump('h2o')">H2O</button>
          <button id="btn-drain" onclick="togglePump('drain')">DRAIN</button>
          <button id="btn-mixer" onclick="togglePump('mixer')">MIXER</button>
          <button id="btn-s1" onclick="togglePump('s1')">S1</button>
          <button id="btn-s2" onclick="togglePump('s2')">S2</button>
          <button id="btn-s3" onclick="togglePump('s3')">S3</button>
          <button id="btn-s4" onclick="togglePump('s4')">S4</button>
        </div>
      </div>
      <div class="card">
        <h3>WiFi credenciales</h3>
        <div class="grid">
          <label>Red (SSID)
            <input id="wifiSsid" type="text" maxlength="32" autocomplete="off">
          </label>
          <label>Password
            <input id="wifiPass" type="text" maxlength="64" autocomplete="off">
          </label>
        </div>
        <div class="row" style="margin-top:10px;">
          <label style="flex-direction:row; align-items:center; gap:8px;">
            <input id="wifiAutoReconnect" type="checkbox">
            Auto-reconectar
          </label>
        </div>
        <div class="btns" style="margin-top:10px;">
          <button class="primary" onclick="applyWifi()">Apply</button>
        </div>
      </div>
    </div>

    <div class="card">
      <h3>Tiempos</h3>
      <div class="grid">
        <label>H2O fill (s)<br><input id="t-h2o" type="number" min="0" step="1"></label>
        <label>Sample fill (s)<br><input id="t-sample" type="number" min="0" step="1"></label>
        <label>Drain (s)<br><input id="t-drain" type="number" min="0" step="1"></label>
        <label>Sample timeout (s)<br><input id="t-sample-timeout" type="number" min="0" step="1"></label>
        <label>Drain timeout (s)<br><input id="t-drain-timeout" type="number" min="0" step="1"></label>
        <label>O2 stabilization (s)<br><input id="t-stab-o2" type="number" min="0" step="1"></label>
        <label>pH stabilization (s)<br><input id="t-stab-ph" type="number" min="0" step="1"></label>
        <label>Samples (0-4)<br><input id="t-sample-count" type="number" min="0" max="4" step="1"></label>
      </div>
      <div class="btns" style="margin-top:10px;">
        <button onclick="loadTimes()">Obtener tiempos actuales</button>
        <button class="primary" onclick="saveTimes()">Guardar tiempos</button>
      </div>
    </div>

    <div class="card">
      <h3>Consola</h3>
      <div class="console" id="consoleBox">--</div>
    </div>
  </main>

  <script>
    async function fetchStatus() {
      try {
        const res = await fetch('/api/status');
        const data = await res.json();

        document.getElementById('phValue').textContent = Number.isFinite(data.ph) ? data.ph.toFixed(3) : '--';
        document.getElementById('o2Value').textContent = Number.isFinite(data.o2) ? (data.o2.toFixed(3) + ' mg/L') : '--';
        document.getElementById('tempValue').textContent = Number.isFinite(data.temp_c) ? (data.temp_c.toFixed(1) + ' C') : '--';
        document.getElementById('autoState').textContent = data.auto_running ? 'AUTO' : 'MANUAL';
        document.getElementById('lastResult').textContent = data.last_result || '--';

        document.getElementById('ipValue').textContent = data.ip || '--';
        document.getElementById('rssiValue').textContent = (data.rssi !== null && data.rssi !== undefined) ? (data.rssi + ' dBm') : '--';

        setLevel('lvlO2', 'lvlO2Text', data.levels?.o2);
        setLevel('lvlPH', 'lvlPHText', data.levels?.ph);
        setLevel('lvlH2O', 'lvlH2OText', data.levels?.h2o);
        setLevel('lvlKCL', 'lvlKCLText', data.levels?.kcl);

        const phMethod = data.cal?.ph?.method || '--';
        const o2Method = data.cal?.o2?.method || '--';
        document.getElementById('calPhMethod').textContent = phMethod;
        document.getElementById('calO2Method').textContent = o2Method;

        let phVals = '--';
        if (phMethod === '3p') {
          phVals = `V4=${data.cal?.ph?.v4?.toFixed(3)} V7=${data.cal?.ph?.v7?.toFixed(3)} V10=${data.cal?.ph?.v10?.toFixed(3)} T=${data.cal?.ph?.tcal?.toFixed(1)}`;
        } else if (phMethod === '2p') {
          phVals = `V7=${data.cal?.ph?.v7?.toFixed(3)} V4=${data.cal?.ph?.v4?.toFixed(3)} T=${data.cal?.ph?.tcal?.toFixed(1)}`;
        }
        document.getElementById('calPhVals').textContent = phVals;

        let o2Vals = '--';
        if (o2Method === '2p') {
          o2Vals = `V1=${data.cal?.o2?.v1?.toFixed(1)} T1=${data.cal?.o2?.t1?.toFixed(1)} V2=${data.cal?.o2?.v2?.toFixed(1)} T2=${data.cal?.o2?.t2?.toFixed(1)}`;
        } else if (o2Method === '1p') {
          o2Vals = `V=${data.cal?.o2?.v1?.toFixed(1)} T=${data.cal?.o2?.t1?.toFixed(1)}`;
        }
        document.getElementById('calO2Vals').textContent = o2Vals;

        setLevel('pKCL', 'pKCLText', data.pumps?.kcl);
        setLevel('pH2O', 'pH2OText', data.pumps?.h2o);
        setLevel('pDrain', 'pDrainText', data.pumps?.drain);
        setLevel('pMixer', 'pMixerText', data.pumps?.mixer);
        setPumpBtn('btn-kcl', data.pumps?.kcl);
        setPumpBtn('btn-h2o', data.pumps?.h2o);
        setPumpBtn('btn-drain', data.pumps?.drain);
        setPumpBtn('btn-mixer', data.pumps?.mixer);
        setPumpBtn('btn-s1', data.pumps?.s1);
        setPumpBtn('btn-s2', data.pumps?.s2);
        setPumpBtn('btn-s3', data.pumps?.s3);
        setPumpBtn('btn-s4', data.pumps?.s4);

        if (data.wifi_ssid !== undefined) {
          const ssidEl = document.getElementById('wifiSsid');
          if (ssidEl && !wifiSsidDirty) {
            ssidEl.value = data.wifi_ssid || '';
          }
        }
        if (data.wifi_pass !== undefined) {
          const passEl = document.getElementById('wifiPass');
          if (passEl && !wifiPassDirty) {
            passEl.value = data.wifi_pass || '';
          }
        }
        if (data.wifi_auto_reconnect !== undefined) {
          const autoEl = document.getElementById('wifiAutoReconnect');
          if (autoEl) autoEl.checked = !!data.wifi_auto_reconnect;
        }

      } catch (e) {
        console.log('status err', e);
      }
    }

    async function doAction(action, value) {
      try {
        const body = value ? ('action=' + encodeURIComponent(action) + '&value=' + encodeURIComponent(value))
                           : ('action=' + encodeURIComponent(action));
        const res = await fetch('/api/action', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: body
        });
        await res.json();
        fetchStatus();
        if (action === 'clear_logs') {
          const box = document.getElementById('consoleBox');
          if (box) box.textContent = '';
        }
      } catch (e) {
        console.log('action err', e);
      }
    }

    function togglePump(id) {
      doAction('toggle_pump', id);
    }

    function applyWifi() {
      const ssid = document.getElementById('wifiSsid')?.value || '';
      const pass = document.getElementById('wifiPass')?.value || '';
      const payload = JSON.stringify({ ssid: ssid, pass: pass });
      doAction('wifi_apply', payload);
      wifiSsidDirty = false;
      wifiPassDirty = false;
    }

    let wifiSsidDirty = false;
    let wifiPassDirty = false;

    const wifiSsidEl = document.getElementById('wifiSsid');
    if (wifiSsidEl) {
      wifiSsidEl.addEventListener('input', () => { wifiSsidDirty = true; });
    }
    const wifiPassEl = document.getElementById('wifiPass');
    if (wifiPassEl) {
      wifiPassEl.addEventListener('input', () => { wifiPassDirty = true; });
    }
    const wifiAutoEl = document.getElementById('wifiAutoReconnect');
    if (wifiAutoEl) {
      wifiAutoEl.addEventListener('change', () => {
        const payload = JSON.stringify({ enabled: !!wifiAutoEl.checked });
        doAction('wifi_auto_reconnect', payload);
      });
    }

    function setInput(id, val) {
      const el = document.getElementById(id);
      if (!el) return;
      if (val === null || val === undefined) return;
      el.value = val;
    }

    async function loadTimes() {
      try {
        const res = await fetch('/api/status');
        const data = await res.json();
        if (data.times) {
          setInput('t-h2o', data.times.h2o_fill_s);
          setInput('t-sample', data.times.sample_fill_s);
          setInput('t-drain', data.times.drain_s);
          setInput('t-sample-timeout', data.times.sample_timeout_s);
          setInput('t-drain-timeout', data.times.drain_timeout_s);
          const o2Stab = (data.times.o2_stabilization_s !== undefined && data.times.o2_stabilization_s !== null)
                         ? data.times.o2_stabilization_s
                         : data.times.stabilization_s;
          setInput('t-stab-o2', o2Stab);
          setInput('t-stab-ph', data.times.ph_stabilization_s);
          setInput('t-sample-count', data.times.sample_count);
        }
      } catch (e) {
        console.log('times err', e);
      }
    }

    function saveTimes() {
      const payload = {
        h2o_fill_s: Number(document.getElementById('t-h2o').value || 0),
        sample_fill_s: Number(document.getElementById('t-sample').value || 0),
        drain_s: Number(document.getElementById('t-drain').value || 0),
        sample_timeout_s: Number(document.getElementById('t-sample-timeout').value || 0),
        drain_timeout_s: Number(document.getElementById('t-drain-timeout').value || 0),
        o2_stabilization_s: Number(document.getElementById('t-stab-o2').value || 0),
        ph_stabilization_s: Number(document.getElementById('t-stab-ph').value || 0),
        sample_count: Number(document.getElementById('t-sample-count').value || 0)
      };
      doAction('set_times', JSON.stringify(payload));
    }

    function setLevel(ledId, textId, on) {
      const led = document.getElementById(ledId);
      const txt = document.getElementById(textId);
      const ok = !!on;
      if (ok) led.classList.add('ok'); else led.classList.remove('ok');
      txt.textContent = ok ? 'ON' : 'OFF';
    }

    function setPumpBtn(btnId, on) {
      const btn = document.getElementById(btnId);
      if (!btn) return;
      if (on) {
        btn.classList.add('primary');
      } else {
        btn.classList.remove('primary');
      }
    }

    function appendConsoleLine(line) {
      const box = document.getElementById('consoleBox');
      if (!box) return;
      if (box.textContent === '--') box.textContent = '';
      if (box.textContent.length > 0) {
        box.textContent += '\n';
      }
      box.textContent += line;
      box.scrollTop = box.scrollHeight;
    }

    function startConsoleWs() {
      const proto = (location.protocol === 'https:') ? 'wss' : 'ws';
      const ws = new WebSocket(`${proto}://${location.hostname}:81/`);
      ws.onmessage = (evt) => appendConsoleLine(evt.data);
      ws.onclose = () => setTimeout(startConsoleWs, 1000);
      ws.onerror = () => ws.close();
    }

    fetchStatus();
    startConsoleWs();
    setInterval(fetchStatus, 1500);
  </script>
</body>
</html>
)rawliteral";

WebPortalManager::WebPortalManager(PumpsManager* pumps,
                                   LevelSensorsManager* levels,
                                   UartProto::UARTManager* uart,
                                   ConfigStore* eeprom)
  : server_(80), pumps_(pumps), levels_(levels), uart_(uart), eeprom_(eeprom) {}

bool WebPortalManager::begin() {
  if (started_) return true;

  server_.on("/", HTTP_GET, [this]() { handleIndex_(); });
  server_.on("/api/status", HTTP_GET, [this]() { handleStatus_(); });
  server_.on("/api/action", HTTP_GET, [this]() { handleAction_(); });
  server_.on("/api/action", HTTP_POST, [this]() { handleAction_(); });
  server_.onNotFound([this]() {
    server_.send(404, "text/plain", "Not Found");
  });

  server_.begin();
  started_ = true;
  return true;
}

void WebPortalManager::loop() {
  if (!started_) return;
  server_.handleClient();
}

void WebPortalManager::setActionHandler(ActionHandler handler) {
  actionHandler_ = handler;
}

void WebPortalManager::log(const String& line) {
  ConsoleService::logSink(line);
}

void WebPortalManager::clearLogs() {
  ConsoleService* console = ConsoleService::instance();
  if (console) console->clear();
}

void WebPortalManager::handleIndex_() {
  server_.send_P(200, "text/html", INDEX_HTML);
}

void WebPortalManager::handleStatus_() {
  JsonDocument doc;

  float lastPh = NAN;
  float lastO2 = NAN;
  float lastTemp = NAN;
  bool autoRun = false;
  String lastResult;

  if (uart_) {
    lastPh = uart_->getLastPh();
    lastO2 = uart_->getLastO2();
    lastTemp = uart_->getLastTempC();
    autoRun = uart_->getAutoRunning();
    lastResult = uart_->getLastResult();
  }

  if (isfinite(lastPh)) {
    doc["ph"] = lastPh;
  } else {
    doc["ph"] = nullptr;
  }
  if (isfinite(lastO2)) {
    doc["o2"] = lastO2;
  } else {
    doc["o2"] = nullptr;
  }
  if (isfinite(lastTemp)) {
    doc["temp_c"] = lastTemp;
  } else {
    doc["temp_c"] = nullptr;
  }
  doc["auto_running"] = autoRun;
  doc["last_result"] = lastResult;

  if (WiFi.status() == WL_CONNECTED) {
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
  } else {
    if (WiFi.getMode() & WIFI_AP) {
      doc["ip"] = WiFi.softAPIP().toString();
    } else {
      doc["ip"] = "";
    }
    doc["rssi"] = nullptr;
  }

  JsonObject levels = doc["levels"].to<JsonObject>();
  if (levels_) {
    levels["o2"] = levels_->o2();
    levels["ph"] = levels_->ph();
    levels["h2o"] = levels_->h2o();
    levels["kcl"] = levels_->kcl();
  } else {
    levels["o2"] = false;
    levels["ph"] = false;
    levels["h2o"] = false;
    levels["kcl"] = false;
  }

  JsonObject pumps = doc["pumps"].to<JsonObject>();
  if (pumps_) {
    pumps["kcl"] = pumps_->isOn(PumpId::KCL);
    pumps["h2o"] = pumps_->isOn(PumpId::H2O);
    pumps["drain"] = pumps_->isOn(PumpId::DRAIN);
    pumps["mixer"] = pumps_->isOn(PumpId::MIXER);
    pumps["s1"] = pumps_->isOn(PumpId::SAMPLE1);
    pumps["s2"] = pumps_->isOn(PumpId::SAMPLE2);
    pumps["s3"] = pumps_->isOn(PumpId::SAMPLE3);
    pumps["s4"] = pumps_->isOn(PumpId::SAMPLE4);
  } else {
    pumps["kcl"] = false;
    pumps["h2o"] = false;
    pumps["drain"] = false;
    pumps["mixer"] = false;
    pumps["s1"] = false;
    pumps["s2"] = false;
    pumps["s3"] = false;
    pumps["s4"] = false;
  }

  if (eeprom_) {
    JsonObject cal = doc["cal"].to<JsonObject>();
    JsonObject calPh = cal["ph"].to<JsonObject>();
    JsonObject calO2 = cal["o2"].to<JsonObject>();

    float V4 = NAN, V7 = NAN, V10 = NAN, Tcal = NAN;
    if (eeprom_->hasPH3pt()) {
      eeprom_->getPH3pt(V4, V7, V10, Tcal);
      calPh["method"] = "3p";
      calPh["v4"] = V4;
      calPh["v7"] = V7;
      calPh["v10"] = V10;
      calPh["tcal"] = Tcal;
    } else if (eeprom_->hasPH2pt()) {
      eeprom_->getPH2pt(V7, V4, Tcal);
      calPh["method"] = "2p";
      calPh["v4"] = V4;
      calPh["v7"] = V7;
      calPh["tcal"] = Tcal;
      calPh["v10"] = nullptr;
    } else {
      calPh["method"] = "NoCal";
    }

    float V1 = NAN, T1 = NAN, V2 = NAN, T2 = NAN;
    eeprom_->getO2Cal(V1, T1, V2, T2);
    const bool v1ok = isfinite(V1) && isfinite(T1);
    const bool v2ok = isfinite(V2) && isfinite(T2);
    if (v1ok && v2ok) {
      calO2["method"] = "2p";
      calO2["v1"] = V1;
      calO2["t1"] = T1;
      calO2["v2"] = V2;
      calO2["t2"] = T2;
    } else if (v1ok) {
      calO2["method"] = "1p";
      calO2["v1"] = V1;
      calO2["t1"] = T1;
      calO2["v2"] = nullptr;
      calO2["t2"] = nullptr;
    } else {
      calO2["method"] = "NoCal";
    }

    JsonObject times = doc["times"].to<JsonObject>();
    times["kcl_fill_s"] = (uint32_t)(eeprom_->kclFillMs() / 1000UL);
    times["h2o_fill_s"] = (uint32_t)(eeprom_->h2oFillMs() / 1000UL);
    times["sample_fill_s"] = (uint32_t)(eeprom_->sampleFillMs() / 1000UL);
    times["drain_s"] = (uint32_t)(eeprom_->drainMs() / 1000UL);
    times["sample_timeout_s"] = (uint32_t)(eeprom_->sampleTimeoutMs() / 1000UL);
    times["drain_timeout_s"] = (uint32_t)(eeprom_->drainTimeoutMs() / 1000UL);
    times["o2_stabilization_s"] = (uint32_t)(eeprom_->o2StabilizationMs() / 1000UL);
    times["ph_stabilization_s"] = (uint32_t)(eeprom_->phStabilizationMs() / 1000UL);
    times["sample_count"] = (uint32_t)eeprom_->sampleCount();

    char ssid[ConfigStore::kWifiSsidMax + 1] = {0};
    char pass[ConfigStore::kWifiPassMax + 1] = {0};
    eeprom_->getWifiCredentials(ssid, sizeof(ssid), pass, sizeof(pass));
    doc["wifi_ssid"] = ssid;
    doc["wifi_pass"] = pass;
    doc["wifi_auto_reconnect"] = eeprom_->wifiAutoReconnect();
  } else {
    doc["wifi_ssid"] = "";
    doc["wifi_pass"] = "";
    doc["wifi_auto_reconnect"] = false;
  }

  sendJson_(doc);
}

void WebPortalManager::handleAction_() {
  String action = server_.arg("action");
  String value = server_.arg("value");

  JsonDocument doc;
  if (action.isEmpty()) {
    doc["ok"] = false;
    doc["message"] = "missing action";
    sendJson_(doc);
    return;
  }

  if (action == "clear_logs") {
    clearLogs();
    doc["ok"] = true;
    doc["message"] = "logs cleared";
    sendJson_(doc);
    return;
  }

  if (action == "wifi_apply" && value.isEmpty()) {
    JsonDocument payload;
    payload["ssid"] = server_.arg("ssid");
    payload["pass"] = server_.arg("pass");
    serializeJson(payload, value);
  }

  if (actionHandler_) {
    String msg;
    bool ok = actionHandler_(action, value, msg);
    doc["ok"] = ok;
    doc["message"] = msg;
    sendJson_(doc);
    return;
  }

  doc["ok"] = false;
  doc["message"] = "no handler";
  sendJson_(doc);
}

void WebPortalManager::sendJson_(JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  server_.send(200, "application/json", out);
}
