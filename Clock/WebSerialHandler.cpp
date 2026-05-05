#include "WebSerialHandler.h"

WebSerialHandler WebSerial;

const char WEBSERIAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32 WebSerial</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { background-color: #1e1e1e; color: #d4d4d4; font-family: 'Courier New', Courier, monospace; margin: 0; padding: 20px; }
    h2 { color: #61dafb; border-bottom: 1px solid #333; padding-bottom: 10px; }
    #terminal {
      background-color: #000;
      border: 1px solid #333;
      border-radius: 4px;
      padding: 10px;
      height: 80vh;
      overflow-y: auto;
      white-space: pre-wrap;
      font-size: 14px;
    }
    .log-line { border-bottom: 1px solid #111; }
    .timestamp { color: #888; }
    .level-error { color: #ff4444; font-weight: bold; }
    .level-info { color: #d4d4d4; }
    .level-debug { color: #6db3f2; }
    #controls { margin-top: 10px; display: flex; gap: 10px; align-items: center; flex-wrap: wrap; }
    button { padding: 8px 16px; cursor: pointer; background: #333; color: #fff; border: 1px solid #555; border-radius: 4px; }
    button:hover { background: #444; }
    label { cursor: pointer; user-select: none; margin-right: 8px; }
    input[type="checkbox"] { margin-right: 5px; }
    #status { color: #888; font-size: 12px; margin-left: auto; }
    .connected { color: #0f0 !important; }
    .disconnected { color: #f00 !important; }
    .filter-group { display: flex; gap: 12px; align-items: center; padding: 4px 8px; background: #252525; border-radius: 4px; }
    .filter-label { color: #888; font-size: 12px; }
    .cb-error { accent-color: #ff4444; }
    .cb-info { accent-color: #d4d4d4; }
    .cb-debug { accent-color: #6db3f2; }
  </style>
</head>
<body>
  <div style="display:flex; align-items:center;">
    <h2>WebSerial Console</h2>
    <span id="status" class="disconnected">Disconnected</span>
  </div>
  <div id="terminal"></div>
  <div id="controls">
    <button onclick="clearTerminal()">Clear</button>
    <button id="autoScrollBtn" onclick="toggleAutoScroll()">Auto-Scroll: ON</button>
    <div class="filter-group">
      <span class="filter-label">Show:</span>
      <label><input type="checkbox" id="showError" class="cb-error" checked onchange="applyFilters()"> Error</label>
      <label><input type="checkbox" id="showInfo" class="cb-info" checked onchange="applyFilters()"> Info</label>
      <label><input type="checkbox" id="showDebug" class="cb-debug" checked onchange="applyFilters()"> Debug</label>
    </div>
    <label><input type="checkbox" id="showTime" checked onchange="toggleTimestamp()"> Time</label>
  </div>

  <script>
    var socket;
    var autoScroll = true;
    var showTimestamp = true;
    var terminal = document.getElementById('terminal');
    var statusLabel = document.getElementById('status');

    function init() {
      var wsUrl = "ws://" + window.location.hostname + ":81/";
      socket = new WebSocket(wsUrl);

      socket.onopen = function(e) {
        statusLabel.textContent = "Connected";
        statusLabel.className = "connected";
        log("--- Connected to WebSerial ---\n", "info");
      };

      socket.onclose = function(e) {
        statusLabel.textContent = "Disconnected";
        statusLabel.className = "disconnected";
        log("--- Disconnected ---\n", "info");
        setTimeout(init, 2000);
      };

      socket.onerror = function(e) {
        console.log(e);
      };

      socket.onmessage = function(e) {
        var level = detectLevel(e.data);
        log(e.data, level);
      };
    }

    function detectLevel(msg) {
      if (msg.indexOf('[ERROR]') !== -1) return 'error';
      if (msg.indexOf('[DEBUG]') !== -1) return 'debug';
      return 'info';
    }

    function log(msg, level) {
      var div = document.createElement('div');
      div.className = 'log-line';
      div.setAttribute('data-level', level);

      var html = msg;
      html = html.replace(/\[(\d{2}:\d{2}:\d{2}\.\d{3})\]/, '<span class="timestamp">[$1]</span>');
      html = html.replace(/\[ERROR\]/, '<span class="level-error">[ERROR]</span>');
      html = html.replace(/\[INFO\]/, '<span class="level-info">[INFO]</span>');
      html = html.replace(/\[DEBUG\]/, '<span class="level-debug">[DEBUG]</span>');

      div.innerHTML = html;

      if (!shouldShow(level)) {
        div.style.display = 'none';
      }

      terminal.appendChild(div);

      if (terminal.childElementCount > 2000) {
        terminal.removeChild(terminal.firstChild);
      }
      if (autoScroll) {
        terminal.scrollTop = terminal.scrollHeight;
      }
    }

    function shouldShow(level) {
      if (level === 'error') return document.getElementById('showError').checked;
      if (level === 'info') return document.getElementById('showInfo').checked;
      if (level === 'debug') return document.getElementById('showDebug').checked;
      return true;
    }

    function applyFilters() {
      var lines = terminal.querySelectorAll('.log-line');
      lines.forEach(function(line) {
        var level = line.getAttribute('data-level');
        line.style.display = shouldShow(level) ? '' : 'none';
      });
    }

    function clearTerminal() {
      terminal.innerHTML = "";
    }

    function toggleAutoScroll() {
      autoScroll = !autoScroll;
      var btn = document.getElementById('autoScrollBtn');
      btn.textContent = 'Auto-Scroll: ' + (autoScroll ? 'ON' : 'OFF');
      btn.style.background = autoScroll ? '#2a5a2a' : '#333';
    }

    function toggleTimestamp() {
      showTimestamp = document.getElementById('showTime').checked;
      var timestamps = terminal.querySelectorAll('.timestamp');
      timestamps.forEach(function(ts) {
        ts.style.display = showTimestamp ? '' : 'none';
      });
    }

    window.onload = init;
  </script>
</body>
</html>
)rawliteral";


WebSerialHandler::WebSerialHandler() : _wsServer(81), _initialized(false), _clientCount(0) {
}

void WebSerialHandler::begin(WebServer *server, const char* url) {
    // Register WebSocket event handler
    _wsServer.begin();
    _wsServer.onEvent([this](uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
        this->onWsEvent(num, type, payload, length);
    });

    // Serve the HTML page on the existing WebServer
    server->on(url, HTTP_GET, [server]() {
        server->send_P(200, "text/html", WEBSERIAL_HTML);
    });

    _initialized = true;
}

void WebSerialHandler::loop() {
    if (_initialized) {
        _wsServer.loop();
    }
}

void WebSerialHandler::onWsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            _clientCount++;
            break;
        case WStype_DISCONNECTED:
            if (_clientCount > 0) _clientCount--;
            break;
        default:
            break;
    }
}

size_t WebSerialHandler::write(uint8_t c) {
    return write(&c, 1);
}

size_t WebSerialHandler::write(const uint8_t *buffer, size_t size) {
    if (!_initialized || _clientCount == 0) return size;
    _wsServer.broadcastTXT(buffer, size);
    return size;
}
