#include "dashboard.h"

static const char kDashboardHtml[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Device Console</title>
  <style>
    :root {
      --bg: #101214;
      --panel: #15181b;
      --text: #e7ecef;
      --muted: #9aa4ad;
      --usb: #6ee7ff;
      --modem: #b6f39b;
      --border: #242a2f;
    }
    body {
      margin: 0;
      font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas,
        "Liberation Mono", "Courier New", monospace;
      background: radial-gradient(circle at top, #182028, #0b0d0f 60%);
      color: var(--text);
    }
    header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 16px 20px;
      border-bottom: 1px solid var(--border);
      background: rgba(10, 12, 14, 0.7);
      backdrop-filter: blur(6px);
    }
    h1 {
      font-size: 16px;
      margin: 0;
      letter-spacing: 0.08em;
      text-transform: uppercase;
      color: var(--muted);
    }
    .status {
      font-size: 12px;
      color: var(--muted);
    }
    main {
      padding: 18px 20px 24px;
    }
    .console {
      border: 1px solid var(--border);
      background: var(--panel);
      border-radius: 10px;
      padding: 14px;
      height: 60vh;
      overflow-y: auto;
      box-shadow: 0 10px 30px rgba(0, 0, 0, 0.35);
    }
    .line {
      white-space: pre-wrap;
      line-height: 1.4;
    }
    .usb { color: var(--usb); }
    .modem { color: var(--modem); }
    .muted { color: var(--muted); }
    .controls {
      display: flex;
      gap: 12px;
      margin-top: 12px;
    }
    button {
      background: #1b2228;
      border: 1px solid var(--border);
      color: var(--text);
      padding: 8px 14px;
      border-radius: 6px;
      cursor: pointer;
    }
    button:hover { background: #232a31; }
  </style>
</head>
<body>
  <header>
    <h1>Serial Console</h1>
    <div class="status" id="status">connecting...</div>
  </header>
  <main>
    <div class="console" id="console">
      <div class="line muted">Waiting for messages...</div>
    </div>
    <div class="controls">
      <button id="clear">Clear</button>
    </div>
  </main>
  <script>
    const consoleEl = document.getElementById('console');
    const statusEl = document.getElementById('status');
    const clearBtn = document.getElementById('clear');

    function appendLine(text, cls) {
      const line = document.createElement('div');
      line.className = `line ${cls || ''}`.trim();
      line.textContent = text;
      consoleEl.appendChild(line);
      consoleEl.scrollTop = consoleEl.scrollHeight;
    }

    clearBtn.addEventListener('click', () => {
      consoleEl.innerHTML = '';
    });

    const ws = new WebSocket(`ws://${location.hostname}:81/`);
    ws.onopen = () => {
      statusEl.textContent = 'connected';
      appendLine('WebSocket connected', 'muted');
    };
    ws.onclose = () => {
      statusEl.textContent = 'disconnected';
      appendLine('WebSocket disconnected', 'muted');
    };
    ws.onmessage = (event) => {
      const text = event.data || '';
      if (text.startsWith('[USB]')) {
        appendLine(text, 'usb');
      } else if (text.startsWith('[MODEM]')) {
        appendLine(text, 'modem');
      } else {
        appendLine(text, '');
      }
    };
  </script>
</body>
</html>
)HTML";

void Dashboard::begin(WebServer& server, WebSocketsServer& ws) {
  server_ = &server;
  ws_ = &ws;

  server_->on("/dashboard", HTTP_GET, [this]() { handleDashboard(); });
  server_->on("/", HTTP_GET, [this]() { handleDashboard(); });

  ws_->onEvent([this](uint8_t client, WStype_t type, uint8_t* payload,
                      size_t length) {
    (void)payload;
    (void)length;
    if (type == WStype_CONNECTED) {
      size_t count = modemBuffer_.size();
      for (size_t i = 0; i < count; ++i) {
        String line = modemBuffer_.get(i);
        ws_->sendTXT(client, line);
      }
    }
  });
}

void Dashboard::loop() {
  if (ws_) {
    ws_->loop();
  }
}

void Dashboard::pushLine(DashboardSource src, const String& line) {
  if (!ws_) {
    return;
  }
  String payload;
  payload.reserve(line.length() + 12);
  payload += sourceLabel(src);
  payload += line;
  if (src == DashboardSource::Modem) {
    modemBuffer_.push(payload);
  }
  ws_->broadcastTXT(payload);
}

void Dashboard::handleDashboard() {
  if (!server_) {
    return;
  }
  server_->send_P(200, "text/html", kDashboardHtml);
}

const char* Dashboard::sourceLabel(DashboardSource src) const {
  return (src == DashboardSource::Usb) ? "[USB] " : "[MODEM] ";
}
