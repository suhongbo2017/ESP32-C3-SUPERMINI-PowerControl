#include <Arduino.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"

// Fill these in before station-mode testing. Empty values intentionally use AP mode.
constexpr char WIFI_SSID[] = "";
constexpr char WIFI_PASSWORD[] = "";

WebServer server(80);

bool pulseActive = false;
uint32_t pulseStartedAt = 0;
uint32_t pulseCount = 0;
String lastAction = "none";
String networkMode = "booting";

String chipSuffix() {
  uint64_t mac = ESP.getEfuseMac();
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%04X", static_cast<unsigned>(mac & 0xFFFF));
  return String(suffix);
}

bool requireAuth() {
  if (!OTA_ENABLED) return true;
  if (server.authenticate(OTA_USERNAME, OTA_PASSWORD)) return true;
  server.requestAuthentication(BASIC_AUTH, "ESP32-C3 PC Power");
  return false;
}

void finishPulseIfDue() {
  if (!pulseActive) return;
  if (millis() - pulseStartedAt < POWER_PULSE_MS) return;

  digitalWrite(POWER_PULSE_PIN, LOW);
  pulseActive = false;
  lastAction = "pulse_completed";
  Serial.println("PULSE completed");
}

bool startPulse() {
  finishPulseIfDue();
  if (pulseActive) {
    lastAction = "pulse_rejected_busy";
    return false;
  }

  // GPIO1 is driven only after an explicit command and returns LOW asynchronously.
  digitalWrite(POWER_PULSE_PIN, HIGH);
  pulseStartedAt = millis();
  pulseActive = true;
  pulseCount++;
  lastAction = "pulse_started";
  Serial.println("PULSE accepted");
  return true;
}

void sendJsonStatus() {
  finishPulseIfDue();
  String json = "{";
  json += "\"pulse_active\":" + String(pulseActive ? "true" : "false");
  json += ",\"pulse_count\":" + String(pulseCount);
  json += ",\"last_action\":\"" + lastAction + "\"";
  json += ",\"network_mode\":\"" + networkMode + "\"";
  json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += ",\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\"";
  json += ",\"rssi\":" + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
  json += "}";
  server.send(200, "application/json", json);
}

const char MAIN_PAGE[] PROGMEM = R"HTML(
<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-C3 PC Power</title>
<style>
:root { color-scheme: dark; font-family: system-ui, sans-serif; }
body { max-width: 560px; margin: 2rem auto; padding: 0 1rem; background:#111827; color:#f3f4f6; }
main { background:#1f2937; border:1px solid #374151; border-radius:12px; padding:1.25rem; }
h1 { font-size:1.35rem; margin-top:0; }
.grid { display:grid; grid-template-columns:1fr 1fr; gap:.6rem; margin:1rem 0; }
.item { background:#111827; padding:.7rem; border-radius:8px; }
.label { color:#9ca3af; font-size:.8rem; }
.value { margin-top:.25rem; word-break:break-word; }
button, a { display:inline-block; border:0; border-radius:8px; padding:.75rem 1rem; font-size:1rem; text-decoration:none; cursor:pointer; }
button { background:#dc2626; color:white; width:100%; }
button:disabled { opacity:.5; cursor:not-allowed; }
a { background:#374151; color:#f3f4f6; margin-top:.75rem; }
#message { min-height:1.5rem; margin-top:.8rem; color:#fbbf24; }
small { color:#9ca3af; }
</style>
</head>
<body>
<main>
<h1>ESP32-C3 PC Power</h1>
<p><small>仅执行一次 300ms 电源键短按，不支持长按强制关机。</small></p>
<div class="grid">
<div class="item"><div class="label">网络</div><div class="value" id="network">-</div></div>
<div class="item"><div class="label">IP</div><div class="value" id="ip">-</div></div>
<div class="item"><div class="label">脉冲状态</div><div class="value" id="active">-</div></div>
<div class="item"><div class="label">执行次数</div><div class="value" id="count">-</div></div>
<div class="item"><div class="label">最近动作</div><div class="value" id="last">-</div></div>
<div class="item"><div class="label">RSSI</div><div class="value" id="rssi">-</div></div>
</div>
<button id="pulse" onclick="pulse()">执行电源键短按</button>
<div id="message"></div>
<a href="/ota">固件 OTA 更新</a>
</main>
<script>
const $ = id => document.getElementById(id);
async function refresh() {
  try {
    const r = await fetch('/api/status', {cache:'no-store'});
    const s = await r.json();
    $('network').textContent = s.network_mode;
    $('ip').textContent = s.ip !== '0.0.0.0' ? s.ip : s.ap_ip;
    $('active').textContent = s.pulse_active ? '执行中' : '关闭';
    $('count').textContent = s.pulse_count;
    $('last').textContent = s.last_action;
    $('rssi').textContent = s.rssi ? s.rssi + ' dBm' : '-';
    $('pulse').disabled = s.pulse_active;
  } catch (e) { $('message').textContent = '状态读取失败'; }
}
async function pulse() {
  $('pulse').disabled = true;
  $('message').textContent = '正在执行...';
  try {
    const r = await fetch('/api/pulse', {method:'POST'});
    const data = await r.json();
    $('message').textContent = data.accepted ? '已接受，脉冲执行中' : '未执行：' + data.reason;
  } catch (e) { $('message').textContent = '命令发送失败'; }
  setTimeout(refresh, 350);
}
refresh(); setInterval(refresh, 1000);
</script>
</body>
</html>
)HTML";

void handleRoot() {
  if (!requireAuth()) return;
  server.send(200, "text/html; charset=utf-8", MAIN_PAGE);
}

void handlePulse() {
  if (!requireAuth()) return;
  bool accepted = startPulse();
  String json = accepted ? "{\"accepted\":true}" : "{\"accepted\":false,\"reason\":\"busy\"}";
  server.send(accepted ? 202 : 409, "application/json", json);
}

void handleOtaPage() {
  if (!requireAuth()) return;
  const char page[] PROGMEM = R"HTML(
<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>OTA</title></head>
<body style="font-family:sans-serif;max-width:560px;margin:2rem auto;padding:1rem"><h1>固件 OTA 更新</h1><p>请选择 PlatformIO 编译生成的 <code>firmware.bin</code>。</p><form method="POST" action="/ota" enctype="multipart/form-data"><input type="file" name="firmware" accept=".bin" required><button type="submit">上传并重启</button></form><p><a href="/">返回控制页</a></p></body></html>
)HTML";
  server.send(200, "text/html; charset=utf-8", page);
}

void handleOtaUpload() {
  if (!requireAuth()) return;
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("OTA start: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) Serial.printf("OTA success: %u bytes\n", upload.totalSize);
    else Update.printError(Serial);
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    Serial.println("OTA aborted");
  }
  yield();
}

void handleOtaResult() {
  if (!requireAuth()) return;
  bool ok = !Update.hasError();
  server.send(ok ? 200 : 500, "text/plain; charset=utf-8", ok ? "OTA completed, rebooting" : "OTA failed");
  if (ok) {
    delay(500);
    ESP.restart();
  }
}

void startAccessPoint() {
  String name = "PC-Power-C3-" + chipSuffix();
  WiFi.mode(WIFI_AP);
  WiFi.softAP(name.c_str(), AP_PASSWORD);
  networkMode = "ap";
  Serial.printf("WiFi AP: %s\n", name.c_str());
  Serial.printf("AP URL: http://%s/\n", WiFi.softAPIP().toString().c_str());
}

void connectNetwork() {
  if (strlen(WIFI_SSID) == 0) {
    startAccessPoint();
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting WiFi: %s\n", WIFI_SSID);
  uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    networkMode = "station";
    Serial.printf("WiFi IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("WiFi failed; starting AP");
    startAccessPoint();
  }
}

void setupRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, []() {
    if (!requireAuth()) return;
    sendJsonStatus();
  });
  server.on("/api/pulse", HTTP_POST, handlePulse);
  server.on("/ota", HTTP_GET, handleOtaPage);
  server.on("/ota", HTTP_POST, handleOtaResult, handleOtaUpload);
  server.onNotFound([]() {
    if (!requireAuth()) return;
    server.send(404, "text/plain; charset=utf-8", "Not found");
  });
}

void handleSerial() {
  static String line;
  while (Serial.available()) {
    char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      line.trim();
      if (line.equalsIgnoreCase("PULSE")) {
        if (!startPulse()) Serial.println("PULSE rejected: busy");
      } else if (line.equalsIgnoreCase("HELP") || line == "?") {
        Serial.println("Commands: PULSE, STATUS, HELP");
      } else if (line.equalsIgnoreCase("STATUS")) {
        finishPulseIfDue();
        Serial.printf("STATUS active=%s count=%lu last=%s mode=%s\n", pulseActive ? "true" : "false", static_cast<unsigned long>(pulseCount), lastAction.c_str(), networkMode.c_str());
      } else if (line.length() > 0) {
        Serial.println("Unknown command. Use HELP");
      }
      line = "";
    } else if (c >= 32 && c <= 126 && line.length() < 32) {
      line += c;
    }
  }
}

void setup() {
  // Set the safe output state before starting any peripheral or network work.
  pinMode(POWER_PULSE_PIN, OUTPUT);
  digitalWrite(POWER_PULSE_PIN, LOW);

  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("ESP32-C3 PC Power controller");
  Serial.println("GPIO1 safe state: LOW");

  connectNetwork();
  setupRoutes();
  server.begin();
  Serial.println("Web server started");
  Serial.println("Commands: PULSE, STATUS, HELP");
}

void loop() {
  finishPulseIfDue();
  handleSerial();
  server.handleClient();
}
