/*
  esp32-s3-8ch-relayboard-RGB.ino — Waveshare ESP32-S3-PoE-ETH-8DI-8RO
  Relays via TCA9554 + single WS2812 status LED using FastLED-backed RgbLed class.
  Adds WiFi + mDNS + ArduinoOTA + a Web UI / JSON API with radio buttons for digital inputs and relay buttons.
  Digital inputs are independent from relays.

  Network priority: Ethernet → WiFi STA → WiFi AP fallback.
  UI reflects the active network interface (Ethernet IP or WiFi IP/SSID).
  Toggle the relay self-test by defining DEBUG_RELAYS.

  Changes (2025-10-13):
  - Fixed Web UI by ensuring correct HTTP handling in ethHandleClient_() and server.handleClient().
  - Added logging for HTTP requests to debug UI issues.
  - Fixed RGB LED in Ethernet mode by ensuring detectAndApplyNetMode_() sets MODE_ETH.
  - Conditioned WebServer and OTA to WiFi/AP modes to prevent crashes in Ethernet mode.
  - Used raw int values for Ethernet.linkStatus() (1=LinkON, 0=LinkOFF).
  - Kept relay logic non-inverted (HIGH=ON, LOW=OFF) per user feedback.
  - Kept watchdog disabled for debugging.
  - Enhanced logging for Ethernet, HTTP, and mode detection.
*/

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include "StateHelpers.h"
#include <SPI.h>
#include <Ethernet.h>
#include "esp_mac.h"
#include <Wire.h>
#include <TCA9554.h>
#include <FastLED.h>
#include "BoardPins.h"
#include "RgbLed_WS2812.h"
#include <WiFiUdp.h>
#include <vector>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <PubSubClient.h>

// One log line
struct UILogLine {
  uint32_t id;
  uint32_t ms;
  String   txt;
};

// ---- Event log (RAM ring buffer) ----
class UILogger : public Print {
public:
  explicit UILogger(size_t maxLines = 200) : _maxLines(maxLines) {}

  void attachMirror(Print* p) { _mirror = p; }

  size_t write(uint8_t c) override {
    if (_mirror) _mirror->write(c);
    if (c == '\n') {
      flushLine();
    } else {
      _cur += (char)c;
    }
    return 1;
  }

  size_t write(const uint8_t* b, size_t s) override {
    if (_mirror) _mirror->write(b, s);
    for (size_t i = 0; i < s; ++i) {
      char c = (char)b[i];
      if (c == '\n') {
        flushLine();
      } else {
        _cur += c;
      }
    }
    return s;
  }

  void collectTail(size_t n, std::vector<UILogLine>& out) {
    if (_buf.empty()) return;
    size_t start = (_buf.size() > n) ? (_buf.size() - n) : 0;
    out.insert(out.end(), _buf.begin() + start, _buf.end());
  }

  uint32_t lastId() const { return _lastId; }

  void pushLine(const String& s) {
    _cur += s;
    flushLine();
  }

  void clear() {
    _buf.clear();
    _lastId = 0;
  }

private:
  void flushLine() {
    if (_cur.length() == 0) return;
    UILogLine L;
    L.id  = ++_lastId;
    L.ms  = millis();
    L.txt = _cur;
    _cur = String();
    if (_buf.size() >= _maxLines) _buf.erase(_buf.begin());
    _buf.push_back(std::move(L));
  }

  Print* _mirror = nullptr;
  String _cur;
  std::vector<UILogLine> _buf;
  size_t _maxLines;
  uint32_t _lastId = 0;
};

UILogger UI_SERIAL(200);

extern "C" int ui_vprintf(const char* fmt, va_list ap) {
  static String line;
  char buf[256];
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  if (n <= 0) return n;

  if (&::Serial) ::Serial.write((const uint8_t*)buf, (size_t)n);

  for (int i = 0; i < n; ++i) {
    char c = buf[i];
    if (c == '\n') { UI_SERIAL.pushLine(line); line = String(); }
    else if (c != '\r') { line += c; }
  }
  return n;
}

extern RgbLed rgb;

enum NetMode { MODE_NONE, MODE_AP, MODE_WIFI, MODE_ETH };
NetMode g_mode = MODE_NONE;

void detectAndApplyNetMode_() {
  NetMode m = MODE_NONE;

  #ifdef ETHERNET_H
  if (Ethernet.linkStatus() == 1 && Ethernet.localIP() != IPAddress(0,0,0,0)) {
    m = MODE_ETH;
    UI_SERIAL.println("[NET] Selected Ethernet mode");
  }
  #endif

  if (m == MODE_NONE && WiFi.status() == WL_CONNECTED) {
    m = MODE_WIFI;
    UI_SERIAL.println("[NET] Selected WiFi STA mode");
  }

  if (m == MODE_NONE && (WiFi.getMode() == WIFI_MODE_AP || WiFi.softAPIP() != IPAddress(0,0,0,0))) {
    m = MODE_AP;
    UI_SERIAL.println("[NET] Selected WiFi AP mode");
  }

  if (m != g_mode) {
    g_mode = m;
    UI_SERIAL.printf("[NET] Mode changed to %d (0=None, 1=AP, 2=WiFi, 3=ETH)\n", (int)g_mode);
    if (g_mode == MODE_AP)
      rgb.setForMask(1);   // Red blink = AP mode
    else if (g_mode == MODE_WIFI)
      rgb.setForMask(2);   // Green blink = WiFi
    else if (g_mode == MODE_ETH)
      rgb.setForMask(3);   // Blue blink = Ethernet
    else
      rgb.setForMask(0);   // Off = No network
  }
}

#include "Information.h"

// ---------------- WiFi / Host Config ----------------
#ifndef WIFI_SSID
  #define WIFI_SSID   "wifi_ssid"
#endif
#ifndef WIFI_PASS
  #define WIFI_PASS   "wifi_password"
#endif
#ifndef HOSTNAME
  #define HOSTNAME    "relayboard"
#endif

// ---------------- TCA9554 Address -------------------
#ifndef TCA9554_ADDR
  #define TCA9554_ADDR 0x20
#endif

// ---------------- MQTT Configuration ----------------
#ifndef MQTT_ENABLED
  #define MQTT_ENABLED 1
#endif
#ifndef MQTT_BROKER_IP
  #define MQTT_BROKER_IP IPAddress(192, 168, 0, 94)
#endif
#ifndef MQTT_PORT
  #define MQTT_PORT 1883
#endif
#ifndef MQTT_USER
  #define MQTT_USER "mqtt_username"
#endif
#ifndef MQTT_PASS
  #define MQTT_PASS "mqtt_password"
#endif
#ifndef MQTT_CLIENT_ID
  #define MQTT_CLIENT_ID "mqtt_client_id"
#endif
#ifndef MQTT_BASE_TOPIC
  #define MQTT_BASE_TOPIC "relayboard"
#endif
#ifndef MQTT_STATE_INTERVAL
  #define MQTT_STATE_INTERVAL 5000
#endif
#ifndef MQTT_RECONNECT_INTERVAL
  #define MQTT_RECONNECT_INTERVAL 5000
#endif

// ---------------- Globals ---------------------------
volatile bool g_ota_ready = false;
static uint16_t g_ota_port = 3232;

WebServer server(80);
EthernetServer ethServer(80);

RgbLed rgb;
TCA9554 tca(TCA9554_ADDR);

// MQTT Clients
WiFiClient wifiClient;
EthernetClient ethClient;
PubSubClient mqttClient;
bool g_mqtt_first_loop = true;
bool g_mqtt_connected = false;
uint32_t g_mqtt_last_attempt = 0;
uint32_t g_mqtt_last_state_publish = 0;

struct QueryParams { String idx; String on; String value; };

String buildStateJson();
void parseQuery_(const String& path, String& route, QueryParams& q);
uint8_t g_mask = 0;
static uint32_t g_nextWalkMs = 0;

String buildLogJson();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void mqttReconnect();
void mqttPublishDevice();
void mqttPublishRelayState(uint8_t idx);
void mqttPublishAllRelayStates();
void mqttPublishInputState(uint8_t idx);
void mqttPublishAllInputStates();
void mqttSetup();
void mqttLoop();

// -------------------- Web UI ------------------------
const char HTML_INDEX[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>ESP32-S3 Relay Board</title>
  <style>
    :root {
      --card-w: 980px;
      --gap: 12px;
      --radius: 14px;
      --shadow: 0 4px 14px rgba(0,0,0,.08);
      --bg: #f6f7fb;
      --card: #fff;
      --text: #111;
      --muted: #6b7280;
      --border: #e5e7eb;+
      --brand: #2563eb;
      --brand-weak: #dbeafe;
      --ok: #10b981;
      --ok-weak: #d1fae5;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0; padding: 24px;
      font-family: system-ui, -apple-system, Segoe UI, Roboto, "Helvetica Neue", Arial, "Noto Sans", "Liberation Sans", sans-serif;
      color: var(--text);
      background: var(--bg);
      display: flex; justify-content: center;
    }
    .container { width: 100%; max-width: var(--card-w); }
    .card {
      background: var(--card);
      border: 1px solid var(--border);
      border-radius: var(--radius);
      box-shadow: var(--shadow);
      padding: 16px 16px 18px;
      margin-bottom: 16px;
    }
    h1 {
      font-size: 1.4rem; margin: 0 0 6px 0;
    }
    .sub {
      font-size: .95rem; color: var(--muted);
      display:flex; gap: 10px; flex-wrap: wrap;
    }
    .label { font-weight:600; color: #374151; }
    .mask { font-variant-numeric: tabular-nums; }
    .section-title {
      font-weight: 700; font-size: 1.02rem;
      margin: 2px 0 12px 0; color:#374151;
      border-bottom: 1px solid var(--border); padding-bottom: 6px;
    }
    .inputs-grid {
      display:grid; grid-template-columns: repeat(8,1fr);
      gap: var(--gap);
    }
    @media (max-width: 680px) {
      .inputs-grid { grid-template-columns: repeat(4,1fr); }
    }
    .di-pill {
      border: 1px solid var(--border);
      border-radius: 999px;
      padding: 8px 10px;
      display:flex; align-items:center; gap:10px;
      background:#fff;
    }
    .dot {
      width: 14px; height:14px; border-radius:50%;
      background:#cbd5e1; border:1px solid #94a3b8;
      box-shadow: inset 0 1px 2px rgba(0,0,0,.08);
    }
    .dot.high { background: var(--ok); border-color:#059669; box-shadow: 0 0 0 3px var(--ok-weak); }
    .relay-grid {
      display:grid; gap: var(--gap);
      grid-template-columns: repeat(4, minmax(140px, 1fr));
    }
    @media (max-width: 680px) {
      .relay-grid { grid-template-columns: repeat(2, 1fr); }
    }
    button.relay {
      appearance:none; outline:none;
      border:1px solid var(--border);
      background: #fff;
      border-radius: 12px;
      padding: 12px 10px;
      font-weight: 600; color:#374151;
      cursor:pointer;
      transition: transform .05s ease, border-color .15s ease, box-shadow .15s ease;
    }
    button.relay:hover { border-color:#c7cad0; }
    button.relay:active { transform: translateY(1px); }
    button.relay.active {
      border-color:#1f2937;
      box-shadow: inset 0 0 0 2px var(--brand-weak);
      background: #f8fafc;
      color:#111827;
    }
    .toolbar { display:flex; gap:10px; }
    .btn {
      appearance:none; border:1px solid var(--border);
      background:#fff; color:#111; border-radius:10px;
      padding:10px 12px; cursor:pointer;
      transition: background .2s ease, border-color .2s ease;
    }
    .btn:hover { background:#f3f4f6; border-color:#d1d5db; }
  </style>
</head>
<body>
  <div class="container">
    <div class="card">
      <h1>ESP32-S3 8-Relay + RGB</h1>
      <div class="sub">
        <div><span class="label">Interface:</span> <span id="iface">None</span></div>
        <div><span class="label">IP:</span> <span id="ip">0.0.0.0</span></div>
        <div><span class="label">SSID:</span> <span id="ssid"></span></div>
        <div><span class="label">Mask:</span> <span class="mask" id="mask">0x00</span></div>
        <div><span class="label">MQTT:</span> <span id="mqtt">Disconnected</span></div>
      </div>
    </div>

    <div class="card">
      <div class="section-title">Digital Inputs</div>
      <div class="inputs-grid" id="inputs"></div>
    </div>

    <div class="card">
      <div class="section-title">Relays</div>
      <div id="buttons" class="relay-grid"></div>
    </div>

    <div class="card toolbar">
      <button id="rebootBtn" class="btn">Reboot</button>
    </div>

    <div class="card">
      <div class="section-title" style="display:flex;align-items:center;justify-content:space-between;">
        <span>Messages</span>
        <button class="btn" onclick="clearLog()">Clear</button>
      </div>
      <div id="messageBox" style="
        padding: 10px;
        border: 1px solid var(--border);
        border-radius: 8px;
        background-color: #f9fafb;
        font-family: monospace;
        font-size: 14px;
        height: 120px;
        overflow-y: auto;
      ">
        <span id="msgContent">Ready.</span>
      </div>
    </div>

    <script>
      var S = { mask:0, di_mask:0, relays:[], di:[], count:8, net:{iface:'None', ip:'0.0.0.0', ssid:''} };
      var prevDI = [];

      function fmtMillis(ms) {
        const s  = Math.floor(ms / 1000);
        const hh = String(Math.floor(s / 3600) % 24).padStart(2,'0');
        const mm = String(Math.floor((s % 3600) / 60)).padStart(2,'0');
        const ss = String(s % 60).padStart(2,'0');
        return `${hh}:${mm}:${ss}`;
      }

      async function fetchAndRenderLog() {
        try {
          const r = await fetch('/api/log');
          if (!r.ok) throw new Error('log fetch failed: ' + r.status);
          const data = await r.json();
          const el = document.getElementById('msgContent');
          if (!el) return;
          el.innerHTML = (data || []).map(o => `[${fmtMillis(o.t)}] ${o.m}`).join('<br>');
          el.scrollTop = el.scrollHeight;
        } catch (e) {
          console.error(e);
<!--           updateMessage('Log fetch error: ' + e.message); -->
        }
      }

      function updateMessage(msg) {
        const el = document.getElementById('msgContent');
        if (el) { el.innerHTML += '<br>' + msg; el.scrollTop = el.scrollHeight; }
      }

      function clearLog() {
        fetch('/api/log/clear', { method: 'POST' })
          .then(() => {
            const el = document.getElementById('msgContent');
            if (el) el.innerHTML = '';
          })
          .catch(e => console.error('Clear failed:', e));
      }

      function drawRelays(){
        var wrap = document.getElementById('buttons');
        var html = '';
        for (var i=0; i<S.count; i++){
          var on = ((S.mask >> i) & 1) === 1;
          html += '<button class="relay '+(on?'active':'')+'" id="relayBtn-'+i+'" data-idx="'+i+'" data-state="'+(on?1:0)+'">R'+(i+1)+': '+(on?'ON':'OFF')+'</button>';
        }
        wrap.innerHTML = html;
        for (let i=0; i<S.count; i++){
          let b = document.getElementById('relayBtn-'+i);
          b.addEventListener('click', function(){
            var cur = (b.getAttribute('data-state') === '1');
            var next = cur ? 0 : 1;
            
            // Optimistic UI update - update immediately for instant feedback
            b.setAttribute('data-state', String(next));
            b.classList.toggle('active', !!next);
            b.textContent = 'R'+(i+1)+': '+(next? 'ON':'OFF');
            
            // Update state object immediately
            if (next) {
              S.mask |= (1 << i);
            } else {
              S.mask &= ~(1 << i);
            }
            paintNet(); // Update mask display
            
            updateMessage(`Relay R${i+1} set to ${next ? 'ON' : 'OFF'}`);
            
            // Send command to server (background)
            var body = new URLSearchParams({ index: String(i), state: String(next) });
            fetch('/api/relay', { method:'POST', headers: {'Content-Type':'application/x-www-form-urlencoded'}, body })
              .then(function(r){
                if (!r.ok) throw new Error('relay post failed: ' + r.status);
                // Success - polling will keep us in sync
              })
              .catch(e => {
                // On error, revert the optimistic update
                b.setAttribute('data-state', String(cur));
                b.classList.toggle('active', !!cur);
                b.textContent = 'R'+(i+1)+': '+(cur? 'ON':'OFF');
                if (cur) {
                  S.mask |= (1 << i);
                } else {
                  S.mask &= ~(1 << i);
                }
                paintNet();
                updateMessage('Error: ' + e.message);
              });
          });
        }
      }

      function drawInputs(){
        var wrap = document.getElementById('inputs');
        var html = '';
        for (var i=0; i<S.di.length; i++){
          var active = (S.di[i] === 0);
          html += '<div class="di-pill">'
                + '<div class="dot ' + (active ? 'high' : '') + '" id="di-dot-' + i + '"></div>'
                + '<div>DI' + (i+1) + '</div>'
                + '</div>';
        }
        wrap.innerHTML = html;
      }

      function paintInputs(){
        for (var i=0; i<S.di.length; i++){
          var dot = document.getElementById('di-dot-'+i);
          if (dot) dot.classList.toggle('high', (S.di[i] === 0));
        }
      }

      function paintNet(){
        var iface = document.getElementById('iface');
        var ip = document.getElementById('ip');
        var ssid = document.getElementById('ssid');
        var m = document.getElementById('mask');
        if (iface) iface.textContent = S.net.iface;
        if (ip) ip.textContent = S.net.ip;
        if (ssid) ssid.textContent = S.net.ssid || '';
        if (m) m.textContent = '0x' + S.mask.toString(16).toUpperCase().padStart(2,'0');
        var mqtt = document.getElementById('mqtt');
        if (mqtt) {
          mqtt.textContent = S.mqtt_connected ? 'Connected' : 'Disconnected';
          mqtt.style.color = S.mqtt_connected ? 'var(--ok)' : '#ef4444';
        }
      }

      function refresh(){
        fetch('/api/state').then(r => r.json()).then(j => {
          S = j;
          paintNet();
          if (!document.getElementById('relayBtn-0')) drawRelays();
          if (!document.getElementById('di-dot-0')) drawInputs();
          paintInputs();
          if (prevDI.length === S.di.length) {
            for (let i = 0; i < S.di.length; i++) {
              if (S.di[i] !== prevDI[i]) {
                const active = (S.di[i] === 0);
                updateMessage(`DI${i+1} changed → ${active ? 'ACTIVE' : 'INACTIVE'}`);
              }
            }
          }
          prevDI = S.di.slice();
        })
        .catch(e => updateMessage("State fetch error: " + e.message));
      }

      document.getElementById('rebootBtn').addEventListener('click', function(){
        fetch('/reboot').then(() => updateMessage("Reboot command sent."));
      });

      setInterval(refresh, 500);  // Poll every 500ms for fast sync
      window.addEventListener('load', () => {
        refresh();
        fetchAndRenderLog();
        setInterval(fetchAndRenderLog, 2000);  // Log updates every 2s
      });
    </script>
  </div>
</body>
</html>
)rawliteral";

void clearLog_() {
  UI_SERIAL.clear();
  UI_SERIAL.pushLine("[LOG] Cleared via /api/log/clear");
}

static String _jsonEscape(const String& s){
  String o; o.reserve(s.length()+8);
  for (char c : s) {
    if (c == '\"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\r' || c == '\n') { /* skip */ }
    else o += c;
  }
  return o;
}

String buildLogJson() {
  std::vector<UILogLine> v;
  UI_SERIAL.collectTail(200, v);
  String json = "[";
  bool first = true;
  for (auto& l : v){
    if (!first) json += ",";
    first = false;
    json += "{\"t\":" + String(l.ms)
         +  ",\"m\":\"" + _jsonEscape(l.txt) + "\"}";
  }
  json += "]";
  return json;
}

static void tcaInit() {
  UI_SERIAL.println("[TCA] Initializing I2C...");
  Wire.begin(BoardPins::I2C_SDA, BoardPins::I2C_SCL);
  UI_SERIAL.println("[TCA] Checking TCA9554 connection...");
  if (!tca.isConnected()) {
    UI_SERIAL.println(F("[ERR] TCA9554 not connected"));
    while (true) delay(1000);
  }
  UI_SERIAL.println("[TCA] Setting TCA9554 outputs...");
  tca.write8(0x00); // All relays OFF (LOW=OFF)
  tca.pinMode8(0x00); // All pins as outputs
  g_mask = 0;
  rgb.setForMask(g_mask);
  UI_SERIAL.println("[TCA] Initialization complete");
}

static void applyMaskToRelays(uint8_t mask) {
  mask &= (uint8_t)((1u << BoardPins::RELAY_COUNT) - 1u);
  uint8_t diff = g_mask ^ mask;
  for (uint8_t i = 0; i < BoardPins::RELAY_COUNT; ++i) {
    const bool on = (mask >> i) & 0x1;
    tca.write1(i, on ? HIGH : LOW); // HIGH=ON, LOW=OFF
    if (diff & (1u << i)) {
      UI_SERIAL.printf("Relay R%d set to %s\n", i + 1, on ? "ON" : "OFF");
    }
  }
  g_mask = mask;
  rgb.setForMask(g_mask);
  if (g_mqtt_connected) {
    mqttPublishAllRelayStates();
  }
}

static void setRelay(uint8_t idx, bool on) {
  if (idx >= BoardPins::RELAY_COUNT) return;
  tca.write1(idx, on ? HIGH : LOW); // HIGH=ON, LOW=OFF
  if (on) g_mask |= (1u << idx);
  else    g_mask &= ~(1u << idx);
  rgb.setForMask(g_mask);
  UI_SERIAL.printf("Relay R%d set to %s\n", idx + 1, on ? "ON" : "OFF");
  if (g_mqtt_connected) {
    mqttPublishRelayState(idx);
  }
}

static String ipStr(const IPAddress& ip) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  return String(buf);
}

static void handleIndex() {
  UI_SERIAL.println("[HTTP] Serving /index");
  server.send(200, "text/html", HTML_INDEX);
}

static void handleState() {
//  UI_SERIAL.println("[HTTP] Serving /api/state");
  server.send(200, "application/json", buildStateJson());
}

static void handleRelay() {
//  UI_SERIAL.println("[HTTP] Handling /api/relay");
  if (!server.hasArg("index") || !server.hasArg("state")) {
    UI_SERIAL.println("[HTTP] Error: Missing index or state");
    server.send(400, "text/plain", "Missing index or state");
    return;
  }
  uint8_t idx = (uint8_t) server.arg("index").toInt();
  String s = server.arg("state");
  bool on = (s == "on" || s == "1" || s == "true");
  if (idx >= BoardPins::RELAY_COUNT) {
    UI_SERIAL.println("[HTTP] Error: index out of range");
    server.send(400, "text/plain", "index out of range");
    return;
  }
  setRelay(idx, on);
  handleState();
}

static void handleMask() {
  UI_SERIAL.println("[HTTP] Handling /api/mask");
  if (!server.hasArg("value")) {
    UI_SERIAL.println("[HTTP] Error: Missing value");
    server.send(400, "text/plain", "Missing value");
    return;
  }
  uint8_t v = (uint8_t) server.arg("value").toInt();
  applyMaskToRelays(v);
  handleState();
}

static bool startAPFallback() {
  String apName = String(HOSTNAME) + "-AP";
  const char *pass = "esp32s3rgb";
  WiFi.mode(WIFI_AP);
  bool success = WiFi.softAP(apName.c_str(), pass);
  if (success) {
    Serial.printf("[WiFi] AP mode: SSID=%s  PASS=%s  IP=%s\n",
                  apName.c_str(), pass, ipStr(WiFi.softAPIP()).c_str());
    return true;
  } else {
    Serial.println("[WiFi] AP setup failed");
    return false;
  }
}

static bool wifiBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[WiFi] Connecting to \"%s\" ...\n", WIFI_SSID);

  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 15000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] Connected: IP=%s RSSI=%d dBm\n",
                  ipStr(WiFi.localIP()).c_str(), WiFi.RSSI());
    return true;
  }
  return false;
}

static bool mdnsBegin() {
  UI_SERIAL.println("[mDNS] Starting mDNS...");
  if (g_mode == MODE_NONE) {
    UI_SERIAL.println("[mDNS] No network interface available, skipping mDNS");
    return false;
  }
  UI_SERIAL.println("[mDNS] Waiting for network stability...");
  delay(1000);
  UI_SERIAL.println("[mDNS] Calling MDNS.begin...");
  if (MDNS.begin(HOSTNAME)) {
    UI_SERIAL.println("[mDNS] Adding HTTP service...");
    MDNS.addService("http", "tcp", 80);
    UI_SERIAL.printf("[mDNS] http://%s.local/ started\n", HOSTNAME);
    return true;
  } else {
    UI_SERIAL.println("[mDNS] MDNS.begin failed");
    return false;
  }
}

static void otaBegin() {
  ArduinoOTA.setPort(g_ota_port);
  ArduinoOTA.setHostname(HOSTNAME);

  ArduinoOTA
    .onStart([]() {
      Serial.println("[OTA] onStart()");
    })
    .onEnd([]() {
      Serial.println("[OTA] onEnd()");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      const uint32_t pct = total ? (progress * 100U) / total : 0U;
      Serial.printf("[OTA] %u%%\n", (unsigned)pct);
    })
    .onError([](ota_error_t error) {
      Serial.printf("[OTA] onError %u\n", (unsigned)error);
      if (error == OTA_AUTH_ERROR)    Serial.println("[OTA] Auth Failed");
      else if (error == OTA_BEGIN_ERROR)   Serial.println("[OTA] Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("[OTA] Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("[OTA] Receive Failed");
      else if (error == OTA_END_ERROR)     Serial.println("[OTA] End Failed");
    });

  UI_SERIAL.println("[OTA] Initializing OTA...");
  ArduinoOTA.begin();
  g_ota_ready = true;

  IPAddress ip = (g_mode == MODE_WIFI) ? WiFi.localIP() : WiFi.softAPIP();
  String ssid = (g_mode == MODE_WIFI) ? WiFi.SSID() : String(HOSTNAME) + "-AP";
  UI_SERIAL.printf("[OTA] Ready on %s:%u (SSID:\"%s\" RSSI:%d)\n",
                ip.toString().c_str(),
                (unsigned)g_ota_port,
                ssid.c_str(),
                (g_mode == MODE_WIFI) ? WiFi.RSSI() : 0);
}

static const uint32_t ETH_SETUP_TIMEOUT = 5000;

String urlDecode_(String v) {
  v.replace("+"," "); return v;
}

void parseQuery_(const String& path, String& route, QueryParams& q) {
  int qm = path.indexOf('?');
  route = (qm>=0) ? path.substring(0,qm) : path;
  String qs = (qm>=0) ? path.substring(qm+1) : String();
  while (qs.length()) {
    int amp = qs.indexOf('&');
    String pair = (amp>=0)? qs.substring(0,amp) : qs;
    int eq = pair.indexOf('=');
    String key = (eq>=0)? pair.substring(0,eq) : pair;
    String val = (eq>=0)? pair.substring(eq+1) : String();
    key = urlDecode_(key); val = urlDecode_(val);
    if (key == "idx" || key == "index") q.idx = val;  // Support both idx and index
    else if (key == "on" || key == "state") q.on = val;  // Support both on and state
    else if (key == "value") q.value = val;
    if (amp<0) break; else qs.remove(0, amp+1);
  }
}

void ethSend_(EthernetClient &c, const String& body, const char* ctype="text/plain") {
  c.println("HTTP/1.1 200 OK");
  c.print("Content-Type: "); c.println(ctype);
  c.print("Content-Length: "); c.println(body.length());
  c.println("Connection: close");
  c.println();
  c.print(body);  // Use print, not println, to avoid extra newline
  delay(1);  // Give W5500 time to transmit
  c.flush();
  delay(1);  // Give W5500 time to complete transmission
}

void ethSendProgmem_(EthernetClient &c, const char* progmemBody, const char* ctype="text/plain") {
  // Calculate length by reading from PROGMEM
  size_t len = strlen_P(progmemBody);
  
  c.println("HTTP/1.1 200 OK");
  c.print("Content-Type: "); c.println(ctype);
  c.print("Content-Length: "); c.println(len);
  c.println("Connection: close");
  c.println();
  
  // Send body in chunks from PROGMEM
  const size_t CHUNK = 512;
  char buf[CHUNK + 1];
  size_t pos = 0;
  while (pos < len) {
    size_t chunkSize = min(CHUNK, len - pos);
    memcpy_P(buf, progmemBody + pos, chunkSize);
    buf[chunkSize] = 0;
    c.print(buf);
    pos += chunkSize;
  }
  c.flush();
}

void ethSendLog_(EthernetClient &c) {
  // Stream log JSON without building a huge String in memory
  // Use chunked transfer encoding to avoid Content-Length calculation
  c.println("HTTP/1.1 200 OK");
  c.println("Content-Type: application/json");
  c.println("Transfer-Encoding: chunked");
  c.println("Connection: close");
  c.println();
  
  std::vector<UILogLine> v;
  UI_SERIAL.collectTail(200, v);
  
  // Start JSON array
  String chunk = "[";
  bool first = true;
  
  for (auto& l : v) {
    // Build one log entry with comma if needed
    String entry = "";
    if (!first) entry += ",";
    first = false;
    entry += "{\"t\":" + String(l.ms) + ",\"m\":\"" + _jsonEscape(l.txt) + "\"}";
    
    // If chunk + entry is getting large, send current chunk first
    if (chunk.length() + entry.length() > 512) {
      // Send current chunk
      c.println(String(chunk.length(), HEX));
      c.print(chunk);
      c.println();
      chunk = "";  // Reset for next chunk
    }
    
    chunk += entry;
  }
  
  // Close JSON array
  chunk += "]";
  
  // Send final chunk
  c.println(String(chunk.length(), HEX));
  c.print(chunk);
  c.println();
  
  // Send terminating chunk
  c.println("0");
  c.println();
  c.flush();
}

bool bringupEthernet_() {
  UI_SERIAL.println(F("\n[ETH] Starting W5500…"));

  UI_SERIAL.printf("[ETH] SPI pins  MOSI=%d  MISO=%d  SCLK=%d  CS=%d\n",
                   BoardPins::SPI_MOSI, BoardPins::SPI_MISO, BoardPins::SPI_SCLK, BoardPins::W5500_CS);
  UI_SERIAL.println("[ETH] Initializing SPI...");
  SPI.begin(BoardPins::SPI_SCLK, BoardPins::SPI_MISO, BoardPins::SPI_MOSI, BoardPins::W5500_CS);
  UI_SERIAL.println("[ETH] Testing SPI communication...");
  digitalWrite(BoardPins::W5500_CS, LOW);
  bool spiOk = SPI.transfer(0x00) != 0xFF;
  digitalWrite(BoardPins::W5500_CS, HIGH);
  if (!spiOk) {
    UI_SERIAL.println("[ETH] SPI communication failed");
    return false;
  }
  UI_SERIAL.println("[ETH] SPI test passed");

  UI_SERIAL.println("[ETH] Initializing Ethernet...");
  Ethernet.init(BoardPins::W5500_CS);
  UI_SERIAL.println("[ETH] Ethernet.init complete");

  UI_SERIAL.println("[ETH] Loading MAC...");
  byte mac[6];
  BoardPins::loadEfuseMac(mac);
  UI_SERIAL.printf("[ETH] MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  UI_SERIAL.println("[ETH] MAC loaded");

  UI_SERIAL.println("[ETH] Probing link...");
  {
    uint32_t t0 = millis();
    int linkAttempts = 0;
    while ((millis() - t0) < ETH_SETUP_TIMEOUT) {
      linkAttempts++;
//      UI_SERIAL.println("[ETH] Calling Ethernet.linkStatus...");
      int status = Ethernet.linkStatus();
//      UI_SERIAL.printf("[ETH] Link attempt %d: %s\n", linkAttempts, status == 1 ? "LinkON" : status == 0 ? "LinkOFF" : "Unknown");
      if (status == 1) break;
      delay(50);
    }
    delay(200);
  }

  UI_SERIAL.println("[ETH] Checking final link status...");
  if (Ethernet.linkStatus() != 1) {
    UI_SERIAL.println("[ETH] No link detected");
    return false;
  }
  UI_SERIAL.println("[ETH] Link detected");

  // Choose between DHCP or Static IP
  // To use static IP, uncomment the next 4 lines and set your network details:
  /*
  IPAddress staticIP(192, 168, 0, 180);
  IPAddress gateway(192, 168, 0, 1);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress dns(192, 168, 0, 1);
  */

  #if 0  // Change to #if 1 to enable static IP
    UI_SERIAL.println("[ETH] Using Static IP...");
    Ethernet.begin(mac, staticIP, dns, gateway, subnet);
    UI_SERIAL.printf("[ETH] Static IP set to %u.%u.%u.%u\n",
                    staticIP[0], staticIP[1], staticIP[2], staticIP[3]);
  #else
    UI_SERIAL.println("[ETH] Starting DHCP...");
    Ethernet.begin(mac);
    UI_SERIAL.println("[ETH] Ethernet.begin called");

    {
      const uint32_t WAIT = 4000;
      uint32_t t0 = millis();
      int dhcpAttempts = 0;
      while (millis() - t0 < WAIT) {
        IPAddress ip = Ethernet.localIP();
        dhcpAttempts++;
        UI_SERIAL.printf("[ETH] DHCP attempt %d: IP=%s\n", dhcpAttempts, ipStr(ip).c_str());
        if (ip != INADDR_NONE && ip[0] != 0) break;
        delay(150);
      }
      UI_SERIAL.println();
    }
  #endif

  IPAddress ip = Ethernet.localIP();
  if (ip == INADDR_NONE || ip[0] == 0) {
    UI_SERIAL.println("[ETH] No IP (DHCP failed)");
    return false;
  }

  UI_SERIAL.println("[ETH] Starting HTTP server...");
  ethServer.begin();
  UI_SERIAL.printf("[ETH] HTTP server started on :80\n[ETH] IP: %u.%u.%u.%u\n",
                  ip[0], ip[1], ip[2], ip[3]);
  return true;
}

static void ethHandleClient_() {
  EthernetClient client = ethServer.available();
  if (!client) return;

//  UI_SERIAL.println("[ETH-HTTP] Client connected");

  uint32_t t0 = millis();
  String requestLine = client.readStringUntil('\n');
  requestLine.trim();
//  UI_SERIAL.printf("[ETH-HTTP] Request: %s\n", requestLine.c_str());

  // Read headers until a blank line
  // Also capture Content-Length for POST body reading
  int contentLength = 0;
  while (client.connected() && (millis() - t0 < 2000)) {
    if (client.available()) {
      String headerLine = client.readStringUntil('\n');
      headerLine.trim();
      if (headerLine.length() == 0) {
        // Blank line signals end of headers
        break;
      }
      // Check for Content-Length header
      if (headerLine.startsWith("Content-Length:")) {
        contentLength = headerLine.substring(15).toInt();
//       UI_SERIAL.printf("[ETH-HTTP] Content-Length: %d\n", contentLength);
      }
      // Optionally log headers for debugging
      // UI_SERIAL.printf("[ETH-HTTP] Header: %s\n", headerLine.c_str());
    }
  }

  bool isGET = requestLine.startsWith("GET ");
  bool isPOST = requestLine.startsWith("POST ");
  int s = requestLine.indexOf(' ');
  int e = requestLine.indexOf(' ', s + 1);
  String path = (s >= 0 && e > s) ? requestLine.substring(s + 1, e) : "/";
//  UI_SERIAL.printf("[ETH-HTTP] Path: %s\n", path.c_str());

  String route;
  QueryParams q;
  parseQuery_(path, route, q);

  // For POST requests, read and parse the body
  String postBody = "";
  if (isPOST && contentLength > 0) {
 //   UI_SERIAL.printf("[ETH-HTTP] Reading POST body (%d bytes)...\n", contentLength);
    // Read exactly contentLength bytes
    int bytesRead = 0;
    uint32_t bodyStart = millis();
    while (bytesRead < contentLength && (millis() - bodyStart < 1000)) {
      if (client.available()) {
        char c = client.read();
        postBody += c;
        bytesRead++;
      }
    }
//    UI_SERIAL.printf("[ETH-HTTP] POST body (%d bytes): %s\n", bytesRead, postBody.c_str());
    
    if (postBody.length() > 0) {
      // Use a temporary route to avoid overwriting the original route
      QueryParams postQ;
      String tempRoute = "";  // Temporary variable
      parseQuery_("?" + postBody, tempRoute, postQ);
      // Merge POST params into q (POST body takes precedence over query string)
      if (postQ.idx.length()) q.idx = postQ.idx;
      if (postQ.on.length()) q.on = postQ.on;
      if (postQ.value.length()) q.value = postQ.value;
      UI_SERIAL.printf("[ETH-HTTP] Parsed POST params: idx=%s, on=%s\n", q.idx.c_str(), q.on.c_str());
    }
  }

  if (isGET && route == "/") {
//    UI_SERIAL.println("[ETH-HTTP] Serving /index");
    // Use chunked PROGMEM sender for large HTML to avoid buffer overflow
    ethSendProgmem_(client, HTML_INDEX, "text/html");
  } else if (isGET && route == "/api/state") {
//    UI_SERIAL.println("[ETH-HTTP] Serving /api/state");
    ethSend_(client, buildStateJson(), "application/json");
  } else if ((isGET || isPOST) && route == "/api/relay") {
//    UI_SERIAL.println("[ETH-HTTP] Handling /api/relay");
    if (q.idx.length() && q.on.length()) {
      uint8_t idx = (uint8_t) q.idx.toInt();
      // Handle both "on"/"off" and "1"/"0" values
      bool on = (q.on == "1" || q.on == "true" || q.on == "on");
      setRelay(idx, on);
      ethSend_(client, buildStateJson(), "application/json");
    } else {
      UI_SERIAL.printf("[ETH-HTTP] Error: Missing params (idx=%s, on=%s)\n", q.idx.c_str(), q.on.c_str());
      client.println("HTTP/1.1 400 Bad Request");
      client.println("Connection: close");
      client.println();
      client.println("Missing index or state");
    }
  } else if ((isGET || isPOST) && route == "/api/mask") {
//    UI_SERIAL.println("[ETH-HTTP] Handling /api/mask");
    if (q.value.length()) {
      uint8_t v = (uint8_t) q.value.toInt();
      applyMaskToRelays(v);
      ethSend_(client, buildStateJson(), "application/json");
    } else {
      UI_SERIAL.println("[ETH-HTTP] Error: Missing value");
      client.println("HTTP/1.1 400 Bad Request");
      client.println("Connection: close");
      client.println();
      client.println("Missing value");
    }
  } else if (isGET && route == "/reboot") {
    UI_SERIAL.println("[ETH-HTTP] Handling /reboot");
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/plain");
    client.println("Connection: close");
    client.println();
    client.println("rebooting");
    client.flush();
    delay(200);
    ESP.restart();
  } else if (isGET && route == "/api/log") {
//    UI_SERIAL.println("[ETH-HTTP] Serving /api/log");
    ethSendLog_(client);
  } else {
    UI_SERIAL.println("[ETH-HTTP] Error: 404 Not Found");
    client.println("HTTP/1.1 404 Not Found");
    client.println("Connection: close");
    client.println();
    client.println("Not Found");
  }
  delay(1);
  client.stop();
//  UI_SERIAL.println("[ETH-HTTP] Client disconnected");
}
static void early_init_logging() {
  ::Serial.begin(115200);
  UI_SERIAL.attachMirror(&::Serial);
  #undef  Serial
  #define Serial UI_SERIAL
  esp_log_set_vprintf(&ui_vprintf);
}

// ============================================================================
// MQTT Functions
// ============================================================================

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String payloadStr = "";
  for (unsigned int i = 0; i < length; i++) {
    payloadStr += (char)payload[i];
  }
  
  UI_SERIAL.printf("[MQTT] Message arrived [%s]: %s\n", topic, payloadStr.c_str());
  
  String topicStr = String(topic);
  
  if (topicStr.startsWith(String(MQTT_BASE_TOPIC) + "/relay/")) {
    String remainder = topicStr.substring(strlen(MQTT_BASE_TOPIC) + 7);
    
    if (remainder.startsWith("all/set")) {
      uint8_t newMask = 0;
      
      // Check for special commands
      payloadStr.toUpperCase();
      payloadStr.trim();
      
      if (payloadStr == "CLEAR" || payloadStr == "OFF" || payloadStr == "ALLOFF") {
        // Clear all relays
        UI_SERIAL.println("[MQTT] Clearing all relays (all OFF)");
        applyMaskToRelays(0);
        return;
      } else if (payloadStr == "ALLON") {
        // Turn all relays on
        UI_SERIAL.println("[MQTT] Setting all relays ON");
        applyMaskToRelays(0xFF);
        return;
      }
      
      // Parse mask value
      if (payloadStr.startsWith("{")) {
        int maskIdx = payloadStr.indexOf("\"mask\"");
        if (maskIdx >= 0) {
          int colonIdx = payloadStr.indexOf(":", maskIdx);
          int endIdx = payloadStr.indexOf("}", colonIdx);
          if (colonIdx >= 0 && endIdx >= 0) {
            String maskValue = payloadStr.substring(colonIdx + 1, endIdx);
            maskValue.trim();
            newMask = (uint8_t)maskValue.toInt();
          }
        }
      } else if (payloadStr.startsWith("0x") || payloadStr.startsWith("0X")) {
        newMask = (uint8_t)strtol(payloadStr.c_str(), NULL, 16);
      } else {
        newMask = (uint8_t)payloadStr.toInt();
      }
      
      UI_SERIAL.printf("[MQTT] Setting relay mask to: 0x%02X\n", newMask);
      applyMaskToRelays(newMask);
      
    } else {
      int slashIdx = remainder.indexOf('/');
      if (slashIdx > 0) {
        String relayNumStr = remainder.substring(0, slashIdx);
        uint8_t relayNum = relayNumStr.toInt();
        
        if (relayNum >= 1 && relayNum <= BoardPins::RELAY_COUNT) {
          payloadStr.toUpperCase();
          payloadStr.trim();
          bool turnOn = (payloadStr == "ON" || payloadStr == "1" || payloadStr == "TRUE");
          
          UI_SERIAL.printf("[MQTT] Setting relay %d to %s\n", relayNum, turnOn ? "ON" : "OFF");
          setRelay(relayNum - 1, turnOn);
        }
      }
    }
  }
}

void mqttReconnect() {
  if (g_mqtt_first_loop || !mqttClient.connected() && (millis() - g_mqtt_last_attempt > MQTT_RECONNECT_INTERVAL)) {
    g_mqtt_first_loop = false;
    g_mqtt_last_attempt = millis();
    
    // Use configured broker IP
    IPAddress brokerIP = MQTT_BROKER_IP;
    
    UI_SERIAL.printf("[MQTT] Attempting connection to %d.%d.%d.%d:%d (user: %s)\n",
                     brokerIP[0], brokerIP[1], brokerIP[2], brokerIP[3], MQTT_PORT, MQTT_USER);
    
    String clientId = String(MQTT_CLIENT_ID);
    
    bool connected = false;
    if (strlen(MQTT_USER) > 0) {
      connected = mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS);
    } else {
      connected = mqttClient.connect(clientId.c_str());
    }
    
    if (connected) {
      UI_SERIAL.println("[MQTT] Connected successfully!");
      g_mqtt_connected = true;
      
      // Restore normal timeout after successful connection
      mqttClient.setSocketTimeout(15);
      
      for (uint8_t i = 1; i <= BoardPins::RELAY_COUNT; i++) {
        String topic = String(MQTT_BASE_TOPIC) + "/relay/" + String(i) + "/set";
        mqttClient.subscribe(topic.c_str());
        UI_SERIAL.printf("[MQTT] Subscribed to %s\n", topic.c_str());
      }
      
      String allTopic = String(MQTT_BASE_TOPIC) + "/relay/all/set";
      mqttClient.subscribe(allTopic.c_str());
      UI_SERIAL.printf("[MQTT] Subscribed to %s\n", allTopic.c_str());
      
      mqttPublishDevice();
      mqttPublishAllRelayStates();
      mqttPublishAllInputStates();  // Publish digital input states too
      
    } else {
      UI_SERIAL.printf("[MQTT] Connection failed, rc=%d ", mqttClient.state());
      switch(mqttClient.state()) {
        case -4: UI_SERIAL.println("(Timeout)"); break;
        case -3: UI_SERIAL.println("(Connection lost)"); break;
        case -2: UI_SERIAL.println("(Connect failed)"); break;
        case -1: UI_SERIAL.println("(Disconnected)"); break;
        case 1: UI_SERIAL.println("(Bad protocol)"); break;
        case 2: UI_SERIAL.println("(Bad client ID)"); break;
        case 3: UI_SERIAL.println("(Unavailable)"); break;
        case 4: UI_SERIAL.println("(Bad credentials)"); break;
        case 5: UI_SERIAL.println("(Unauthorized)"); break;
        default: UI_SERIAL.println("(Unknown)"); break;
      }
      g_mqtt_connected = false;
      // Keep short timeout for next attempt
      mqttClient.setSocketTimeout(1);
    }
  }
}

void mqttPublishRelayState(uint8_t idx) {
  if (!mqttClient.connected()) return;
  
  bool state = (g_mask >> idx) & 1;
  String topic = String(MQTT_BASE_TOPIC) + "/relay/" + String(idx + 1) + "/state";
  String payload = state ? "ON" : "OFF";
  
  mqttClient.publish(topic.c_str(), payload.c_str(), true);
}

void mqttPublishAllRelayStates() {
  if (!mqttClient.connected()) return;
  
  for (uint8_t i = 0; i < BoardPins::RELAY_COUNT; i++) {
    mqttPublishRelayState(i);
  }
  
  String topic = String(MQTT_BASE_TOPIC) + "/relay/all/state";
  String payload = "{\"mask\":" + String(g_mask) + ",\"binary\":\"0b";
  for (int i = 7; i >= 0; i--) {
    payload += ((g_mask >> i) & 1) ? "1" : "0";
  }
  payload += "\",\"hex\":\"0x" + String(g_mask, HEX) + "\"}";
  
  mqttClient.publish(topic.c_str(), payload.c_str(), true);
}

void mqttPublishDevice() {
  if (!mqttClient.connected()) return;

  String topic = String(MQTT_BASE_TOPIC) + "/device";
  String payload = buildStateJson();
  mqttClient.publish(topic.c_str(), payload.c_str(), true);
}

void mqttPublishInputState(uint8_t idx) {
  if (!mqttClient.connected()) return;
  
  uint8_t di_mask = readDI_mask();
  bool state = (di_mask >> idx) & 1;
  String topic = String(MQTT_BASE_TOPIC) + "/input/" + String(idx + 1) + "/state";
  String payload = state ? "ON" : "OFF";
  
  mqttClient.publish(topic.c_str(), payload.c_str(), true);
}

void mqttPublishAllInputStates() {
  if (!mqttClient.connected()) return;
  
  uint8_t di_mask = readDI_mask();
  
  for (uint8_t i = 0; i < BoardPins::DI_COUNT; i++) {
    bool state = (di_mask >> i) & 1;
    String topic = String(MQTT_BASE_TOPIC) + "/input/" + String(i + 1) + "/state";
    String payload = state ? "ON" : "OFF";
    mqttClient.publish(topic.c_str(), payload.c_str(), true);
  }
  
  String topic = String(MQTT_BASE_TOPIC) + "/input/all/state";
  String payload = "{\"mask\":" + String(di_mask) + ",\"binary\":\"0b";
  for (int i = 7; i >= 0; i--) {
    payload += ((di_mask >> i) & 1) ? "1" : "0";
  }
  payload += "\",\"hex\":\"0x" + String(di_mask, HEX) + "\"}";
  
  mqttClient.publish(topic.c_str(), payload.c_str(), true);
}


void mqttSetup() {
  // Use configured MQTT broker IP (192.168.0.94 - Ubuntu server)
  IPAddress brokerIP = MQTT_BROKER_IP;
  
  if (g_mode == MODE_ETH) {
    mqttClient.setClient(ethClient);
    UI_SERIAL.printf("[MQTT] Using Ethernet, broker at: %d.%d.%d.%d:%d\n", 
                     brokerIP[0], brokerIP[1], brokerIP[2], brokerIP[3], MQTT_PORT);
  } else if (g_mode == MODE_WIFI) {
    mqttClient.setClient(wifiClient);
    UI_SERIAL.printf("[MQTT] Using WiFi, broker at: %d.%d.%d.%d:%d\n", 
                     brokerIP[0], brokerIP[1], brokerIP[2], brokerIP[3], MQTT_PORT);
  } else {
    // AP mode - no MQTT
    UI_SERIAL.println("[MQTT] AP mode - MQTT disabled");
    return;
  }
  
  UI_SERIAL.printf("[MQTT] Credentials: user=%s\n", MQTT_USER);
  
  mqttClient.setServer(brokerIP, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setKeepAlive(60);
  mqttClient.setSocketTimeout(1);  // Short timeout to avoid blocking
  
  UI_SERIAL.println("[MQTT] MQTT client initialized");
}

void mqttLoop() {
  if (g_mode == MODE_AP) {
    g_mqtt_connected = false;
    return;
  }
  
  static NetMode last_mqtt_mode = MODE_NONE;
  if (g_mode != last_mqtt_mode && g_mode != MODE_AP) {
    if (mqttClient.connected()) {
      mqttClient.disconnect();
    }
    
    // Reconfigure with configured broker IP
    IPAddress brokerIP = MQTT_BROKER_IP;
    if (g_mode == MODE_ETH) {
      mqttClient.setClient(ethClient);
    } else {
      mqttClient.setClient(wifiClient);
    }
    
    mqttClient.setServer(brokerIP, MQTT_PORT);
    mqttClient.setSocketTimeout(1);  // Short timeout
    
    last_mqtt_mode = g_mode;
    UI_SERIAL.printf("[MQTT] Network mode changed, using broker %d.%d.%d.%d:%d\n",
                     brokerIP[0], brokerIP[1], brokerIP[2], brokerIP[3], MQTT_PORT);
  }
  
  if (!mqttClient.connected()) {
    g_mqtt_connected = false;
    mqttReconnect();
  } else {
    g_mqtt_connected = true;
    mqttClient.loop();
    
    if (millis() - g_mqtt_last_state_publish > MQTT_STATE_INTERVAL) {
      g_mqtt_last_state_publish = millis();
      mqttPublishDevice();
      mqttPublishAllRelayStates();
    }
  }
}

void setup() {
  early_init_logging();
  Serial.println("[BOOT] starting…");
  Serial.println("=== ESP32-S3 8-Relay + RGB + ETH/WiFi/OTA/Web ===");
  Serial.printf("[BOOT] Free heap: %u bytes\n", ESP.getFreeHeap());
  Serial.println("[BOOT] Disabling watchdogs...");
  esp_task_wdt_deinit();
  // --- Try Ethernet first ---
  Serial.println("[BOOT] Attempting Ethernet setup...");
  bool ethUp = bringupEthernet_();                 // MUST: calls ethServer.begin() on success
  if (ethUp) {
    g_mode = MODE_ETH;
  } else {
    // --- Then Wi-Fi STA ---
    Serial.println("[BOOT] Ethernet failed, trying WiFi...");
    bool wifiUp = wifiBegin();                     // MUST: connects STA only; DO NOT server.begin() here
    if (wifiUp) {
      g_mode = MODE_WIFI;
    } else {
      // --- Finally AP fallback ---
      Serial.println("[BOOT] WiFi failed, starting AP...");
      startAPFallback();                           // MUST: switches to AP mode; DO NOT server.begin() here
      g_mode = MODE_AP;
    }
  }
  // --- Peripherals (independent of network) ---
  Serial.println("[BOOT] Initializing peripherals...");
  tcaInit();
  rgb.begin();
  rgb.setHeartbeatEnabled(true);
  BoardPins::configInputs();
  // --- MQTT setup ---
  if (MQTT_ENABLED == 1) {
    Serial.println("[BOOT] Initializing MQTT...");
    mqttSetup();
  }
  // --- Register HTTP routes ONCE (works for Wi-Fi/AP server) ---
  // (Safe to register even if we don't call server.begin() yet.)
  server.on("/",              HTTP_GET,  handleIndex);
  server.on("/api/state",     HTTP_GET,  handleState);
  server.on("/api/log",       HTTP_GET,  [](){
//    UI_SERIAL.println("[HTTP] Serving /api/log");
    server.send(200, "application/json", buildLogJson());
  });
  server.on("/api/relay", HTTP_GET, [](void) {
    UI_SERIAL.println("[HTTP] Handling GET /api/relay");
    String indexStr = server.arg("idx").length() > 0 ? server.arg("idx") : server.arg("index");
    String stateStr = server.arg("on").length() > 0 ? server.arg("on") : server.arg("state");
    if (indexStr.length() > 0 && stateStr.length() > 0) {
      uint8_t idx = (uint8_t)indexStr.toInt();
      bool on = (stateStr == "1" || stateStr == "true" || stateStr == "on");
      setRelay(idx, on);  // Assuming this is your relay toggle function
      server.send(200, "application/json", buildStateJson());
    } else {
      server.send(400, "text/plain", "Missing index or state");
    }
  });
  server.on("/api/log/clear", HTTP_POST, [](){
    UI_SERIAL.println("[HTTP] Handling /api/log/clear");
    clearLog_(); server.send(200, "text/plain", "cleared");
  });
  server.on("/api/relay",     HTTP_POST, handleRelay);
  server.on("/api/mask",      HTTP_POST, handleMask);
  server.on("/reboot",        HTTP_GET,  [](){
    UI_SERIAL.println("[HTTP] Handling /reboot");
    server.send(200, "text/plain", "rebooting"); delay(200); ESP.restart();
  });
  server.on("/api/diag", HTTP_GET, [](){
    UI_SERIAL.println("[HTTP] Serving /api/diag");
    String j = "{";
    j += "\"ota_ready\":";  j += (g_ota_ready ? "true":"false"); j += ",";
    j += "\"ota_port\":";   j += String(g_ota_port);             j += ",";
    j += "\"heap\":";       j += String(ESP.getFreeHeap());      j += "}";
    server.send(200, "application/json", j);
  });
  // --- Start the correct HTTP server for the chosen mode ---
  if (g_mode == MODE_WIFI || g_mode == MODE_AP) {
    server.begin();                                  // Wi-Fi/AP HTTP
    Serial.println("[WEB] WiFi/AP HTTP server started on :80");
  } else {
    // MODE_ETH: bringupEthernet_() must already have called ethServer.begin()
    Serial.println("[WEB] Using Ethernet HTTP server on :80");
  }
  // --- OTA only on Wi-Fi/AP (you already disabled ArduinoOTA for ETH) ---
  if (g_mode == MODE_WIFI || g_mode == MODE_AP) {
    otaBegin();
  } else {
    Serial.println("[BOOT] Skipping OTA (Ethernet mode)");
  }
  // Initial LED + status line (so you see IP/mode at boot)
  IPAddress ip = (g_mode == MODE_ETH) ? Ethernet.localIP() : WiFi.localIP();
  rgb.setForMask(g_mode);   // 3=ETH (blue), 2=Wi-Fi (green), 1=AP (red)
  UI_SERIAL.printf("[NET] Serving via %s at %u.%u.%u.%u\n",
                   (g_mode==MODE_ETH)?"Ethernet":(g_mode==MODE_WIFI)?"Wi-Fi":"AP",
                   ip[0], ip[1], ip[2], ip[3]);

  Serial.println("[BOOT] Setup complete");
}

void loop() {
  const uint32_t now = millis();
  // ---------- Service current HTTP endpoint ----------
  switch (g_mode) {
    case MODE_ETH:
      ethHandleClient_();     // Handles Ethernet HTTP requests
      Ethernet.maintain();    // DHCP renew (no-op if static)
      break;
    case MODE_WIFI:
      server.handleClient();  // Handles Wi-Fi HTTP requests
      break;
    case MODE_AP:
      server.handleClient();  // Handles AP HTTP requests
      break;
  }
  rgb.tick();            // heartbeat
  // --- MQTT service ---
  if (MQTT_ENABLED == 1) {
    mqttLoop();
  }
  // ---------- Priority + failover with light debounce ----------
  // We require the new target to be "good" for ~800 ms before switching.
  static uint32_t stableStartMs = 0;
  static uint8_t  pendingMode   = 0;   // 0 = none, else MODE_ETH/WIFI/AP
  auto ethGood  = [](){
    return (Ethernet.linkStatus() == LinkON) &&
           (Ethernet.localIP() != IPAddress(0,0,0,0));
  };
  auto wifiGood = [](){
    return (WiFi.status() == WL_CONNECTED);
  };
  // Choose preferred target given current conditions (no change yet)
  uint8_t want =
      ethGood()  ? MODE_ETH :
      wifiGood() ? MODE_WIFI :
                   MODE_AP;
  // If what we want differs, start debounce; else clear debounce
  if (want != g_mode) {
    if (pendingMode != want) {
      pendingMode   = want;
      stableStartMs = now;
    } else if (now - stableStartMs >= 800) {   // ~0.8s stable desire
      // --- Commit mode switch (start correct server, stop the other) ---
      if (pendingMode == MODE_ETH) {
        bringupEthernet();        // must: ethServer.begin(), stop Wi-Fi server
      } else if (pendingMode == MODE_WIFI) {
        bringupWiFi();            // must: server.begin(), drain/stop ETH clients
      } else { // MODE_AP
        bringupAP();              // must: server.begin(), drain/stop ETH clients
      }
      g_mode = (NetMode)pendingMode;
      pendingMode = 0;
      // fall through to log/IP update below
    }
  } else {
    pendingMode = 0; // we're already in the preferred mode
  }
  // ---------- Log + LED only when mode or IP changes ----------
  static uint8_t   last_mode = 0;                // 0 forces first log
  static IPAddress last_ip(0,0,0,0);
  IPAddress cur_ip = (g_mode == MODE_ETH)
                       ? Ethernet.localIP()
                       : WiFi.localIP();         // works for STA and AP
  if (g_mode != last_mode || cur_ip != last_ip) {
    last_mode = g_mode;
    last_ip   = cur_ip;
    // LED mask: 3=ETH (blue), 2=Wi-Fi (green), 1=AP (red)
    rgb.setForMask(g_mode);
    const char* name =
      (g_mode == MODE_ETH)  ? "Ethernet" :
      (g_mode == MODE_WIFI) ? "Wi-Fi"    : "AP";
    UI_SERIAL.printf("[NET] Serving via %s at %u.%u.%u.%u\n",
                     name, cur_ip[0], cur_ip[1], cur_ip[2], cur_ip[3]);
  }
  // ---------- Digital Input polling (unchanged) ----------
  static uint32_t lastDIPoll = 0;
  static uint8_t  prev_di_mask = readDI_mask();
  if (now - lastDIPoll >= 100) {
    uint8_t cur_di_mask = readDI_mask();
    if (cur_di_mask != prev_di_mask) {
      for (uint8_t i = 0; i < BoardPins::DI_COUNT; ++i) {
        bool was_high = (prev_di_mask >> i) & 1;
        bool is_high  = (cur_di_mask  >> i) & 1;
        if (was_high != is_high) {
          UI_SERIAL.printf("DI%d %s\n", i + 1, is_high ? "deactivated" : "activated");
          // Publish digital input state change via MQTT
          mqttPublishInputState(i);
        }
      }
      // Publish all inputs state after any change
      //mqttPublishAllInputStates();
      prev_di_mask = cur_di_mask;
    }
    lastDIPoll = now;
  }
}

// Drain/stop pending Ethernet HTTP clients
static void stopEthHttp_() {
  while (true) {
    EthernetClient c = ethServer.available();
    if (!c) break;
    c.stop();
  }
}

// Start Ethernet serving; stop Wi-Fi server for exclusivity
inline void bringupEthernet() {
  bringupEthernet_();   // must bring up W5500 (+ DHCP/static)
  server.stop();        // ensure Wi-Fi/AP HTTP is not serving
  // NOTE: make sure ethServer.begin() is called inside bringupEthernet_()
  // If not, add: ethServer.begin();
}

// Start Wi-Fi STA serving; stop Ethernet HTTP for exclusivity
inline void bringupWiFi() {
  wifiBegin();          // connect STA (your existing function)
  stopEthHttp_();       // close any ETH clients
  server.begin();       // start Wi-Fi/AP HTTP
}

// Start AP serving; stop Ethernet HTTP for exclusivity
inline void bringupAP() {
  startAPFallback();    // enable AP mode
  stopEthHttp_();       // close any ETH clients
  server.begin();       // start Wi-Fi/AP HTTP
}

inline uint8_t readDI_mask() {
  uint8_t m = 0;
  for (uint8_t i = 0; i < BoardPins::DI_COUNT; ++i) {
    const uint8_t pin = INPUT_PINS[i];
    if (digitalRead(pin) == HIGH) m |= (1u << i);
  }
  return m;
}

String buildStateJson() {
  String iface;
  IPAddress ip;
  String ssid = "";
  int rssi = 0;

  if (g_mode == MODE_ETH) {
    iface = "Ethernet";
    ip = Ethernet.localIP();
  } else if (g_mode == MODE_WIFI) {
    iface = "WiFi";
    ip = WiFi.localIP();
    ssid = WiFi.SSID();
    rssi = WiFi.RSSI();
  } else if (g_mode == MODE_AP) {
    iface = "WiFi AP";
    ip = WiFi.softAPIP();
    ssid = String(HOSTNAME) + "-AP";
  } else {
    iface = "None";
    ip = IPAddress(0,0,0,0);
  }

  uint8_t di_mask = readDI_mask();
  String json = "{";
  json += "\"mask\":" + String(g_mask) + ",";
  json += "\"mqtt_connected\":" + String(g_mqtt_connected ? "true" : "false") + ",";
  json += "\"di_mask\":" + String(di_mask) + ",";
  json += "\"di\":[";
  for (uint8_t i = 0; i < BoardPins::DI_COUNT; ++i) {
    json += String((di_mask >> i) & 1);
    if (i < BoardPins::DI_COUNT - 1) json += ",";
  }
  json += "],";
  json += "\"count\":" + String(BoardPins::RELAY_COUNT) + ",";
  json += "\"net\":{";
  json += "\"iface\":\"" + _jsonEscape(iface) + "\",";
  json += "\"ip\":\"" + ipStr(ip) + "\",";
  json += "\"ssid\":\"" + _jsonEscape(ssid) + "\",";
  json += "\"rssi\":" + String(rssi) + "}";
  json += "}";
  return json;
}
