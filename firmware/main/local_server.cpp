#include "local_server.hpp"

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "lwip/inet.h"

namespace pc_power {
namespace {
constexpr char kTag[] = "local-server";

struct ServerContext {
  LocalAuth* auth;
  PowerActuator* actuator;
  ReachabilityMonitor* reachability;
  WifiManager* wifi;
  EnvironmentSensor* environment;
  PowerLedMonitor* power_led;
  CloudClient* cloud;
};

struct SetupContext {
  WifiManager* wifi;
};

bool decode_form_component(char* value) {
  if (value == nullptr) return false;
  char* write = value;
  for (char* read = value; *read != '\0'; ++read) {
    if (*read == '+') {
      *write++ = ' ';
    } else if (*read == '%' && read[1] != '\0' && read[2] != '\0') {
      auto hex = [](char digit) -> int {
        if (digit >= '0' && digit <= '9') return digit - '0';
        if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
        if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
        return -1;
      };
      const int high = hex(read[1]);
      const int low = hex(read[2]);
      if (high < 0 || low < 0) return false;
      *write++ = static_cast<char>((high << 4) | low);
      read += 2;
    } else {
      *write++ = *read;
    }
  }
  *write = '\0';
  return true;
}

esp_err_t send_json(httpd_req_t* request, const char* body, int status = 200) {
  httpd_resp_set_status(request, status == 200 ? "200 OK" : "400 Bad Request");
  httpd_resp_set_type(request, "application/json");
  return httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
}
}  // namespace

void LocalServer::stop() {
  if (server_ != nullptr) {
    httpd_stop(server_);
    server_ = nullptr;
  }
  delete static_cast<ServerContext*>(context_);
  context_ = nullptr;
}

void LocalServer::stop_setup() {
  if (setup_server_ != nullptr) {
    httpd_stop(setup_server_);
    setup_server_ = nullptr;
  }
  delete static_cast<SetupContext*>(setup_context_);
  setup_context_ = nullptr;
}

esp_err_t LocalServer::start_setup(WifiManager* wifi) {
  if (wifi == nullptr || setup_server_ != nullptr) return ESP_ERR_INVALID_ARG;
  auto* context = new SetupContext{wifi};
  if (context == nullptr) return ESP_ERR_NO_MEM;
  setup_context_ = context;

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.stack_size = 12288;
  config.max_uri_handlers = 2;
  esp_err_t err = httpd_start(&setup_server_, &config);
  if (err != ESP_OK) {
    delete context;
    setup_context_ = nullptr;
    return err;
  }

  httpd_uri_t page{};
  page.uri = "/";
  page.method = HTTP_GET;
  page.handler = &LocalServer::setup_page_handler;
  page.user_ctx = context;
  err = httpd_register_uri_handler(setup_server_, &page);

  httpd_uri_t submit{};
  submit.uri = "/api/setup/wifi";
  submit.method = HTTP_POST;
  submit.handler = &LocalServer::setup_wifi_handler;
  submit.user_ctx = context;
  if (err == ESP_OK) err = httpd_register_uri_handler(setup_server_, &submit);
  if (err != ESP_OK) {
    stop_setup();
    return err;
  }
  ESP_LOGI(kTag, "setup HTTP server started at 192.168.4.1");
  return ESP_OK;
}

esp_err_t LocalServer::start(LocalAuth* auth, PowerActuator* actuator,
                             ReachabilityMonitor* reachability,
                             WifiManager* wifi, EnvironmentSensor* environment,
                             PowerLedMonitor* power_led, CloudClient* cloud) {
  if (auth == nullptr || actuator == nullptr || reachability == nullptr ||
      wifi == nullptr || environment == nullptr || power_led == nullptr ||
      cloud == nullptr ||
      server_ != nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  auto* context = new ServerContext{auth, actuator, reachability, wifi,
                                     environment, power_led, cloud};
  if (context == nullptr) {
    return ESP_ERR_NO_MEM;
  }

  context_ = context;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 12288;
  config.max_uri_handlers = 10;
  esp_err_t err = httpd_start(&server_, &config);
  if (err != ESP_OK) {
    delete context;
    context_ = nullptr;
    return err;
  }

  httpd_uri_t page_uri{};
  page_uri.uri = "/";
  page_uri.method = HTTP_GET;
  page_uri.handler = &LocalServer::page_handler;
  page_uri.user_ctx = context;
  err = httpd_register_uri_handler(server_, &page_uri);
  if (err != ESP_OK) {
    httpd_stop(server_);
    server_ = nullptr;
    delete context;
    context_ = nullptr;
    return err;
  }

  httpd_uri_t settings_uri{};
  settings_uri.uri = "/settings";
  settings_uri.method = HTTP_GET;
  settings_uri.handler = &LocalServer::settings_page_handler;
  settings_uri.user_ctx = context;
  err = httpd_register_uri_handler(server_, &settings_uri);
  if (err != ESP_OK) {
    httpd_stop(server_);
    server_ = nullptr;
    delete context;
    context_ = nullptr;
    return err;
  }

  httpd_uri_t status_uri{};
  status_uri.uri = "/api/local/status";
  status_uri.method = HTTP_GET;
  status_uri.handler = &LocalServer::status_handler;
  status_uri.user_ctx = context;
  err = httpd_register_uri_handler(server_, &status_uri);
  if (err != ESP_OK) {
    httpd_stop(server_);
    server_ = nullptr;
    delete context;
    context_ = nullptr;
    return err;
  }

  httpd_uri_t config_uri{};
  config_uri.uri = "/api/local/configure";
  config_uri.method = HTTP_POST;
  config_uri.handler = &LocalServer::local_config_handler;
  config_uri.user_ctx = context;
  err = httpd_register_uri_handler(server_, &config_uri);
  if (err != ESP_OK) {
    httpd_stop(server_);
    server_ = nullptr;
    delete context;
    context_ = nullptr;
    return err;
  }

  httpd_uri_t password_uri{};
  password_uri.uri = "/api/local/password";
  password_uri.method = HTTP_POST;
  password_uri.handler = &LocalServer::password_handler;
  password_uri.user_ctx = context;
  err = httpd_register_uri_handler(server_, &password_uri);
  if (err != ESP_OK) {
    httpd_stop(server_);
    server_ = nullptr;
    delete context;
    context_ = nullptr;
    return err;
  }

  httpd_uri_t wifi_uri{};
  wifi_uri.uri = "/api/local/wifi";
  wifi_uri.method = HTTP_POST;
  wifi_uri.handler = &LocalServer::wifi_handler;
  wifi_uri.user_ctx = context;
  err = httpd_register_uri_handler(server_, &wifi_uri);
  if (err != ESP_OK) {
    httpd_stop(server_);
    server_ = nullptr;
    delete context;
    context_ = nullptr;
    return err;
  }

  httpd_uri_t cloud_uri{};
  cloud_uri.uri = "/api/local/cloud";
  cloud_uri.method = HTTP_POST;
  cloud_uri.handler = &LocalServer::cloud_config_handler;
  cloud_uri.user_ctx = context;
  err = httpd_register_uri_handler(server_, &cloud_uri);
  if (err != ESP_OK) {
    httpd_stop(server_);
    server_ = nullptr;
    delete context;
    context_ = nullptr;
    return err;
  }

  httpd_uri_t ping_uri{};
  ping_uri.uri = "/api/local/ping-target";
  ping_uri.method = HTTP_POST;
  ping_uri.handler = &LocalServer::ping_handler;
  ping_uri.user_ctx = context;
  err = httpd_register_uri_handler(server_, &ping_uri);
  if (err != ESP_OK) {
    httpd_stop(server_);
    server_ = nullptr;
    delete context;
    context_ = nullptr;
    return err;
  }

  httpd_uri_t press_uri{};
  press_uri.uri = "/api/local/power-press";
  press_uri.method = HTTP_POST;
  press_uri.handler = &LocalServer::power_press_handler;
  press_uri.user_ctx = context;
  err = httpd_register_uri_handler(server_, &press_uri);
  if (err != ESP_OK) {
    httpd_stop(server_);
    server_ = nullptr;
    delete context;
    context_ = nullptr;
    return err;
  }

  ESP_LOGI(kTag, "local HTTP server started");
  return ESP_OK;
}

esp_err_t LocalServer::setup_send(httpd_req_t* request, const char* body,
                                   const char* status) {
  httpd_resp_set_status(request, status);
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  return httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
}

esp_err_t LocalServer::setup_page_handler(httpd_req_t* request) {
  static constexpr char kPage[] =
      "<!doctype html><html><head><meta charset=utf-8>"
      "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
      "<title>PC Power Wi-Fi Setup</title></head><body>"
      "<h1>Wi-Fi setup</h1>"
      "<form method=post action=\"/api/setup/wifi\">"
      "<label>Wi-Fi name <input name=ssid maxlength=32 required></label><br>"
      "<label>Password <input name=password type=password minlength=8 maxlength=63 required></label><br>"
      "<button type=submit>Save and connect</button></form></body></html>";
  return setup_send(request, kPage);
}

esp_err_t LocalServer::setup_wifi_handler(httpd_req_t* request) {
  auto* context = static_cast<SetupContext*>(request->user_ctx);
  constexpr size_t kMaxSetupBody = 512;
  if (context == nullptr || context->wifi == nullptr || request->content_len <= 0 ||
      request->content_len > kMaxSetupBody) {
    return setup_send(request, "invalid request", "400 Bad Request");
  }
  char body[kMaxSetupBody + 1]{};
  size_t total_received = 0;
  while (total_received < static_cast<size_t>(request->content_len)) {
    const int received = httpd_req_recv(
        request, body + total_received,
        request->content_len - total_received);
    if (received <= 0) {
      std::memset(body, 0, sizeof(body));
      return setup_send(request, "request read failed", "400 Bad Request");
    }
    total_received += static_cast<size_t>(received);
  }

  char ssid[33]{};
  char password[64]{};
  bool ssid_seen = false;
  bool password_seen = false;
  bool form_valid = true;
  char* cursor = body;
  while (form_valid && cursor != nullptr && *cursor != '\0') {
    char* next = std::strchr(cursor, '&');
    if (next != nullptr) *next++ = '\0';
    char* equals = std::strchr(cursor, '=');
    if (equals == nullptr) {
      form_valid = false;
      break;
    }
    *equals++ = '\0';
    char* destination = nullptr;
    size_t destination_size = 0;
    if (std::strcmp(cursor, "ssid") == 0 && !ssid_seen) {
      destination = ssid;
      destination_size = sizeof(ssid);
      ssid_seen = true;
    } else if (std::strcmp(cursor, "password") == 0 && !password_seen) {
      destination = password;
      destination_size = sizeof(password);
      password_seen = true;
    } else {
      form_valid = false;
      break;
    }
    if (!decode_form_component(equals) || std::strlen(equals) >= destination_size) {
      form_valid = false;
      break;
    }
    std::memcpy(destination, equals, std::strlen(equals) + 1);
    cursor = next;
  }
  if (!ssid_seen || !password_seen) form_valid = false;

  esp_err_t err = form_valid ? context->wifi->configure(ssid, password)
                             : ESP_ERR_INVALID_ARG;
  ESP_LOGI(kTag, "setup Wi-Fi submission: ssid_length=%u password_length=%u result=%s",
           static_cast<unsigned>(std::strlen(ssid)),
           static_cast<unsigned>(std::strlen(password)), esp_err_to_name(err));
  std::memset(password, 0, sizeof(password));
  std::memset(body, 0, sizeof(body));
  if (err != ESP_OK) {
    return setup_send(request, "invalid Wi-Fi credentials", "400 Bad Request");
  }
  return setup_send(request, "Wi-Fi credentials saved. The device is connecting.");
}

esp_err_t LocalServer::settings_page_handler(httpd_req_t* request) {
  auto* context = static_cast<ServerContext*>(request->user_ctx);
  if (!authorize(request, context->auth)) return unauthorized(request);
  const char* cloud_state = context->cloud->is_configured() ? "已配对" : "未配对";
  static char kSettingsPage[8192];
  std::snprintf(kSettingsPage, sizeof(kSettingsPage),
      "<!doctype html><html lang=zh-CN><head><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>设备设置</title><style>body{margin:0;background:#101b20;color:#e8f5f1;font:16px/1.6 system-ui,'Microsoft YaHei',sans-serif}main{max-width:620px;margin:40px auto;padding:0 20px}section{background:#17272b;border:1px solid #2d4949;border-radius:8px;padding:24px;margin:16px 0}h1{font-size:28px;margin:12px 0;color:#fff}h2{font-size:18px;color:#8fffe0}p{color:#a9c2bd}.back{color:#8fffe0;text-decoration:none}label{display:block;margin:14px 0 5px;color:#b9d2cc}input{width:100%%;box-sizing:border-box;padding:12px;border:1px solid #55706d;border-radius:6px;background:#0e181c;color:#fff;font:inherit}button{border:0;border-radius:6px;padding:12px 16px;background:#25b99a;color:#06221e;font:inherit;font-weight:700;margin-top:14px;cursor:pointer}.notice{min-height:26px;color:#8fffe0}</style></head><body><main><a class=back href=\"/\">返回设备首页</a><h1>设备设置</h1><p>修改配置后设备可能短暂断开当前网络。</p><section><h2>Wi-Fi 配置</h2><form id=wifi><label>Wi-Fi 名称</label><input name=ssid maxlength=32 required autocomplete=off><label>Wi-Fi 密码</label><input name=password type=password minlength=8 maxlength=63 required autocomplete=new-password><button>保存并重新连接</button></form></section><section><h2>本地登录密码</h2><form id=password><label>新密码（至少 12 位）</label><input name=password type=password minlength=12 maxlength=63 required autocomplete=new-password><button>更新密码</button></form></section><section><h2>云端配对</h2><p>当前状态：%s。输入云端地址、设备 ID 和一次性配对码。设备会自动完成配对并保存凭据。</p><form id=cloud method=post action=/api/local/cloud><label>云端 HTTPS 地址</label><input name=url type=url value=\"https://pc.19831018.xyz\" maxlength=191 required autocomplete=url><label>设备 ID</label><input name=deviceId value=\"pc-power-c2d4\" maxlength=64 required autocomplete=off><label>一次性配对码</label><input name=pairingCode type=password minlength=16 maxlength=64 required autocomplete=one-time-code><button>开始配对</button></form></section><p class=notice id=notice></p></main><script>const n=document.getElementById('notice');async function send(form,url,msg){const button=form.querySelector('button');button.disabled=true;n.textContent='正在提交…';try{const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(form))});const text=await r.text();if(!r.ok)throw Error(text||('HTTP '+r.status));n.textContent=msg;form.reset()}catch(error){n.textContent='提交失败：'+(error.message||'网络错误')}finally{button.disabled=false}}document.getElementById('wifi').onsubmit=e=>{e.preventDefault();send(e.target,'/api/local/wifi','Wi-Fi 配置已保存，设备正在重新连接')};document.getElementById('password').onsubmit=e=>{e.preventDefault();send(e.target,'/api/local/password','登录密码已更新，请使用新密码重新登录')};document.getElementById('cloud').onsubmit=e=>{e.preventDefault();send(e.target,'/api/local/cloud','设备配对成功，云端连接已启用')}</script></body></html>", cloud_state);
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  return httpd_resp_send(request, kSettingsPage, HTTPD_RESP_USE_STRLEN);
}

esp_err_t LocalServer::page_handler(httpd_req_t* request) {
  auto* context = static_cast<ServerContext*>(request->user_ctx);
  static constexpr char kConfiguredPage[] =
      "<!doctype html><html lang=zh-CN><head><meta charset=utf-8>"
      "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
      "<title>电脑电源控制器</title><style>"
      "*{box-sizing:border-box}body{margin:0;background:#eef2f1;color:#172629;font:15px/1.5 system-ui,'Microsoft YaHei',sans-serif}"
      "header{background:#173c3b;color:#fff;padding:22px max(20px,calc((100% - 960px)/2));display:flex;justify-content:space-between;align-items:center;gap:16px}"
      "header h1{font-size:22px;margin:0;font-weight:650}header p{margin:3px 0 0;color:#c6dcda;font-size:13px}main{max-width:960px;margin:24px auto;padding:0 18px}"
      ".notice{padding:11px 14px;border-left:4px solid #16877c;background:#fff;margin-bottom:18px}"
      ".grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}section{background:#fff;border:1px solid #dce4e2;border-radius:6px;padding:18px;min-width:0}"
      "h2{font-size:16px;margin:0 0 14px;color:#234b49}.wide{grid-column:1/-1}.metrics{display:grid;grid-template-columns:repeat(3,1fr);gap:12px}.status-icon{display:inline-grid;place-items:center;width:40px;height:40px;margin-right:10px;border-radius:9px;background:#dcefeb;color:#12695f;font-size:22px;vertical-align:middle;box-shadow:0 0 12px rgba(22,126,114,.28)}.status-icon.warn{background:#fff0d8;color:#a76415;box-shadow:0 0 12px rgba(199,137,46,.35)}.status-icon.off{background:#e8edec;color:#657673;box-shadow:none}"
      ".metric{padding:14px;background:#f3f7f6;border-radius:6px}.label{font-size:13px;color:#647674;display:flex;align-items:center}.value{font-size:18px;font-weight:700;margin-top:6px;overflow-wrap:anywhere}.value.glow{color:#8fffe0;text-shadow:0 0 8px rgba(143,255,224,.8)}.chart-wrap{height:230px;position:relative}.chart-wrap canvas{width:100%;height:100%;display:block}.legend{display:flex;gap:18px;font-size:12px;color:#657673;margin-top:8px}.legend span:before{content:'';display:inline-block;width:18px;height:3px;margin:0 6px 3px 0;background:currentColor}.temp-line{color:#c85d3d}.humidity-line{color:#167e72}"
      "label{display:block;font-size:13px;color:#415653;margin:10px 0 5px}input{display:block;width:100%;max-width:440px;padding:10px;border:1px solid #bbc9c6;border-radius:4px;font:inherit}"
      "button{border:0;border-radius:4px;padding:10px 14px;background:#167e72;color:white;font:inherit;font-weight:600;cursor:pointer;margin-top:8px}button:hover{background:#11665d}"
      ".power{background:#263837;color:#fff}.power h2{color:#dcebe8}.power button{background:#c85d3d}.muted{color:#657673;font-size:13px;margin:0}"
      ".tag{display:inline-block;padding:3px 9px;border-radius:20px;background:#dcefeb;color:#12695f;font-size:12px}.lang-link{color:#dcefeb;text-decoration:none;font-size:13px;margin-right:12px}.button-link{display:inline-block;padding:10px 14px;background:#167e72;color:white;text-decoration:none;border-radius:5px;font-weight:650;margin-top:8px}footer{font-size:12px;color:#71817f;margin:18px 0}"
      "@media(max-width:620px){header{padding:18px;align-items:flex-start}main{margin:16px auto;padding:0 12px}.grid{grid-template-columns:1fr;gap:10px}.wide{grid-column:auto}.metrics{grid-template-columns:1fr 1fr}.value{font-size:16px}}"
      "</style></head><body><header><div><h1>电脑电源控制器</h1><p>本地设备管理</p></div><div><a class=lang-link href=\"/?lang=en\">English</a><span class=tag id=\"connection\">正在连接</span></div></header>"
      "<main><div class=notice id=\"notice\">正在读取设备状态…</div><div class=grid>"
      "<section class=wide><h2><span class=status-icon>▦</span>设备状态</h2><div class=metrics>"
      "<div class=metric><div class=label><span class=status-icon id=\"wifiIcon\">⌁</span>Wi-Fi</div><div class=value id=\"wifiState\">读取中</div></div>"
      "<div class=metric><div class=label><span class=status-icon>◉</span>当前网络</div><div class=value id=\"ssid\">—</div></div>"
      "<div class=metric><div class=label><span class=status-icon>⌂</span>设备 IP</div><div class=value id=\"ip\">—</div></div>"
      "<div class=metric><div class=label><span class=status-icon id=\"reachabilityIcon\">⌁</span>Ping 测试</div><div class=value id=\"pingSummary\">等待测试</div><div class=muted>下次测试：<span id=\"pingCountdown\">30</span> 秒</div></div>"
      "<div class=metric><div class=label><span class=status-icon id=\"pcIcon\">⏻</span>电脑电源 LED</div><div class=value glow id=\"pcState\">读取中</div></div>"
      "<div class=metric><div class=label><span class=\"status-icon off\" id=\"cloudIcon\">☁</span>云端连接</div><div class=value id=\"cloudState\">读取中</div></div>"
      "</div></section>"
      "<section class=power><h2><span class=status-icon>⚡</span>电源控制</h2><p class=muted>操作会真实模拟按下电脑电源键，电脑开关机状态无法由本设备确认。</p>"
      "<button id=\"power\">模拟按下电源键</button><p class=muted id=\"powerCooldown\">每次操作都需要确认</p></section>"
      "<section><h2><span class=status-icon>⌁</span>Ping 目标</h2><p class=muted>设备每 30 秒执行 3 次 Ping。</p><form id=\"ping\"><label>电脑 IPv4 地址</label><input name=ip type=text inputmode=decimal placeholder=\"192.168.1.100\" maxlength=15 pattern=\"([0-9]{1,3}\\.){3}[0-9]{1,3}\" required><button>保存目标地址</button></form><p class=muted>当前目标：<span id=\"pingTarget\">未设置</span></p></section><section><h2><span class=status-icon>♨</span>温湿度</h2><div class=metrics><div class=metric><div class=label>温度</div><div class=value id=\"temperature\">读取中</div></div><div class=metric><div class=label>湿度</div><div class=value id=\"humidity\">读取中</div></div></div></section>"
      "<section class=wide><h2>温湿度曲线</h2><div class=chart-wrap><canvas id=\"environmentChart\"></canvas></div><div class=legend><span class=temp-line>温度 °C</span><span class=humidity-line>湿度 %</span></div><p class=muted>当前浏览器保存最近 2 小时数据；页面关闭期间不会采集新数据。</p></section>"
      "<section><h2><span class=status-icon>⚙</span>设备设置</h2><p class=muted>Wi-Fi 与本地账号密码在独立页面管理。</p><a class=button-link href=\"/settings\">打开设置页面</a></section></div><footer>状态每 5 秒自动刷新。管理连接使用本地 HTTP。</footer></main><script>"
      "const $=id=>document.getElementById(id),notice=$('notice'),historyKey='pc-power-environment-history-v1',maxAge=2*60*60*1000,maxHistory=1440;let history=[];const english=new URLSearchParams(location.search).get('lang')==='en';const tr=(zh,en)=>english?en:zh;function translatePage(){if(!english)return;document.documentElement.lang='en';document.title='PC Power Controller';const map={'电脑电源控制器':'PC Power Controller','本地设备管理':'Local device management','设备状态':'Device status','当前网络':'Current network','设备 IP':'Device IP','Ping 测试':'Ping test','电脑电源 LED':'PC power LED','云端连接':'Cloud connection','电源控制':'Power control','模拟按下电源键':'Simulate power button press','Ping 目标':'Ping target','电脑 IPv4 地址':'PC IPv4 address','保存目标地址':'Save target address','温湿度':'Temperature and humidity','温度':'Temperature','湿度':'Humidity','温湿度曲线':'Temperature and humidity history','设备设置':'Device settings','打开设置页面':'Open settings','等待测试':'Waiting for test','下次测试：':'Next test:','当前目标：':'Current target:','状态每 5 秒自动刷新。管理连接使用本地 HTTP。':'Status refreshes every 5 seconds. Management uses local HTTP.','正在读取设备状态…':'Reading device status…','操作会真实模拟按下电脑电源键，电脑开关机状态无法由本设备确认。':'This physically simulates pressing the PC power button. This device cannot confirm the PC power state.','每次操作都需要确认':'Confirmation is required for every operation','设备每 30 秒执行 3 次 Ping。':'The device performs three Ping attempts every 30 seconds.','当前浏览器保存最近 2 小时数据；页面关闭期间不会采集新数据。':'This browser stores the last two hours of data; no data is collected while the page is closed.','Wi-Fi 与本地账号密码在独立页面管理。':'Wi-Fi and local account settings are managed on a separate page.'};const walker=document.createTreeWalker(document.body,NodeFilter.SHOW_TEXT);const nodes=[];while(walker.nextNode())nodes.push(walker.currentNode);nodes.forEach(n=>{let value=n.nodeValue;Object.keys(map).forEach(key=>{value=value.split(key).join(map[key])});n.nodeValue=value});const link=document.querySelector('.lang-link');if(link){link.textContent='中文';link.href='/'}}"
      "function pruneHistory(){const cutoff=Date.now()-maxAge;while(history.length&&history[0].time<cutoff)history.shift();if(history.length>maxHistory)history.splice(0,history.length-maxHistory)}function restoreHistory(){try{const saved=JSON.parse(localStorage.getItem(historyKey)||'[]');if(Array.isArray(saved))history=saved.filter(p=>p&&Number.isFinite(p.time)&&Number.isFinite(p.temperature)&&Number.isFinite(p.humidity));}catch(e){history=[]}pruneHistory();drawChart()}function saveHistory(){pruneHistory();try{localStorage.setItem(historyKey,JSON.stringify(history))}catch(e){}}"
      "function drawChart(){const canvas=$('environmentChart'),box=canvas.getBoundingClientRect(),ratio=window.devicePixelRatio||1,w=Math.max(1,box.width),h=Math.max(1,box.height);canvas.width=w*ratio;canvas.height=h*ratio;const ctx=canvas.getContext('2d');ctx.scale(ratio,ratio);ctx.clearRect(0,0,w,h);const pad={l:42,r:42,t:20,b:28},cw=w-pad.l-pad.r,ch=h-pad.t-pad.b;ctx.font='11px system-ui,Microsoft YaHei,sans-serif';ctx.fillStyle='#657673';ctx.textAlign='right';ctx.textBaseline='middle';[50,25,0].forEach((v,i)=>{let y=pad.t+ch*i/2;ctx.fillText(v+'°',pad.l-7,y)});ctx.textAlign='left';[100,50,0].forEach((v,i)=>{let y=pad.t+ch*i/2;ctx.fillText(v+'%',w-pad.r+7,y)});ctx.textAlign='center';ctx.textBaseline='top';if(history.length>0){const age=Math.max(0,Date.now()-history[0].time);ctx.fillText('现在',w-pad.r,h-pad.b+8);ctx.fillText('−'+Math.round(age/60000)+' 分钟',pad.l,h-pad.b+8)}ctx.strokeStyle='#dce4e2';ctx.lineWidth=1;ctx.beginPath();for(let i=0;i<3;i++){let y=pad.t+ch*i/2;ctx.moveTo(pad.l,y);ctx.lineTo(w-pad.r,y)}ctx.stroke();if(history.length<2)return;const draw=(key,color,min,max)=>{ctx.strokeStyle=color;ctx.lineWidth=2;ctx.beginPath();history.forEach((p,i)=>{let x=pad.l+cw*i/(history.length-1),y=pad.t+ch*(1-(p[key]-min)/(max-min));i?ctx.lineTo(x,y):ctx.moveTo(x,y)});ctx.stroke()};draw('temperature','#c85d3d',0,50);draw('humidity','#167e72',0,100)}window.addEventListener('resize',drawChart);restoreHistory();"
      "translatePage();async function load(){try{let r=await fetch('/api/local/status');if(r.status===401){$('connection').textContent=tr('需要登录','Login required');notice.textContent=tr('请使用本地设备账号登录','Please log in with the local device account');return}if(!r.ok)throw Error();let s=await r.json();"
      "$('connection').textContent=tr('设备已连接','Device connected');$('wifiState').textContent=s.wifi==='connected'?tr('已连接','Connected'):tr('未连接','Disconnected');$('ssid').textContent=s.ssid==='unavailable'?tr('不可用','Unavailable'):s.ssid;$('ip').textContent=s.ip;$('pingTarget').textContent=s.pingTarget||tr('未设置','Not set');$('pingSummary').textContent=(s.pingTarget||tr('未设置','Not set'))+'：'+(s.reachability==='reachable'?tr('可达','Reachable'):s.reachability==='unreachable'?tr('不可达','Unreachable'):tr('未知','Unknown'));$('pingSummary').className='value '+(s.reachability==='reachable'?'glow':'');$('pingCountdown').textContent=s.pingCountdown;$('temperature').textContent=s.sensorAvailable?s.temperature.toFixed(1)+' °C':tr('传感器不可用','Sensor unavailable');$('humidity').textContent=s.sensorAvailable?s.humidity.toFixed(1)+' %':tr('传感器不可用','Sensor unavailable');if(s.sensorAvailable){history.push({time:Date.now(),temperature:s.temperature,humidity:s.humidity});saveHistory();drawChart()}$('pcState').textContent=s.powerLedState==='blinking'?tr('LED 闪烁（可能为睡眠状态）','LED blinking (possibly sleep state)'):s.powerLedLevel?tr('LED 灯熄灭','LED off'):tr('LED 灯亮','LED on');$('cloudState').textContent=s.cloudOnline?tr('在线','Online'):s.cloudConfigured?tr('已配置，连接中','Configured, connecting'):tr('未配对','Not paired');$('cloudIcon').className='status-icon '+(s.cloudOnline?'':'off');notice.textContent=tr('设备状态正常','Device status is normal');}catch(e){$('connection').textContent=tr('连接失败','Connection failed');notice.textContent=tr('无法读取设备状态，请检查网络连接','Unable to read device status. Check the network connection.');}}"
      "load();setInterval(load,5000);const power=$('power'),cooldown=$('powerCooldown');let cooling=false;power.onclick=async()=>{if(cooling)return;if(!confirm(tr('即将真实模拟按下电脑电源键一次。若电脑已开机，可能触发关机或睡眠。确认继续？','This will physically simulate one press of the PC power button. If the PC is on, it may shut down or enter sleep. Continue?')))return;cooling=true;power.disabled=true;let left=10;cooldown.textContent=tr('操作保护中，请等待 ','Protection active, wait ')+left+tr(' 秒',' s');let timer=setInterval(()=>{left--;cooldown.textContent=left>0?tr('操作保护中，请等待 ','Protection active, wait ')+left+tr(' 秒',' s'):tr('请勿连续触发电源键','Do not press the power button repeatedly');if(left<=0){clearInterval(timer);if(!power.dataset.requesting){power.disabled=false;cooldown.textContent=tr('可以进行下一次操作','Ready for the next operation');cooling=false}}},1000);power.dataset.requesting='1';try{let r=await fetch('/api/local/power-press',{method:'POST'});notice.textContent=r.ok?tr('电源键操作已发送','Power button command sent'):tr('操作失败：','Operation failed:')+await r.text()}catch(e){notice.textContent=tr('请求失败，请检查设备连接','Request failed. Check the device connection.')}finally{delete power.dataset.requesting;if(left<=0){power.disabled=false;cooldown.textContent=tr('可以进行下一次操作','Ready for the next operation');cooling=false}}};"
      "$('ping').onsubmit=async e=>{e.preventDefault();notice.textContent=tr('正在保存 Ping 目标…','Saving Ping target…');let r=await fetch('/api/local/ping-target',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(e.target))});notice.textContent=r.ok?tr('Ping 目标已保存，设备将在 30 秒周期内执行测试','Ping target saved. The device will test it every 30 seconds.'):tr('Ping 配置失败：','Ping configuration failed:')+await r.text();};"
      "</script></body></html>";
  static constexpr char kSetupPage[] =
      "<!doctype html><html lang=zh-CN><head><meta charset=utf-8>"
      "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
      "<title>初始化设备</title><style>"
      "*{box-sizing:border-box}body{margin:0;background:#eef2f1;color:#172629;font:15px system-ui,'Microsoft YaHei',sans-serif}"
      "main{max-width:480px;margin:12vh auto;padding:24px;background:#fff;border:1px solid #dce4e2;border-radius:6px}"
      "h1{font-size:22px;color:#234b49}p{color:#657673}label{display:block;margin:14px 0 5px}input{width:100%;padding:10px;border:1px solid #bbc9c6;border-radius:4px;font:inherit}"
      "button{margin-top:16px;padding:10px 14px;border:0;border-radius:4px;background:#167e72;color:white;font:inherit;font-weight:600}"
      "</style></head><body><main><h1>初始化本地账号</h1><p>首次使用请设置设备管理账号。密码至少 12 位。</p>"
      "<form method=post action=\"/api/local/configure\"><label>用户名</label><input name=username maxlength=32 required autocomplete=username>"
      "<label>登录密码</label><input name=password type=password minlength=12 maxlength=63 required autocomplete=new-password>"
      "<button type=submit>保存账号并继续</button></form></main></body></html>";
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  const char* page = context->auth->is_configured() ? kConfiguredPage : kSetupPage;
  return httpd_resp_send(request, page, HTTPD_RESP_USE_STRLEN);
}

esp_err_t LocalServer::local_config_handler(httpd_req_t* request) {
  auto* context = static_cast<ServerContext*>(request->user_ctx);
  constexpr size_t kMaxBody = 256;
  if (context == nullptr || context->auth == nullptr ||
      context->auth->is_configured() || request->content_len <= 0 ||
      request->content_len > kMaxBody) {
    return setup_send(request, "configuration unavailable", "400 Bad Request");
  }

  char body[kMaxBody + 1]{};
  size_t received_total = 0;
  while (received_total < static_cast<size_t>(request->content_len)) {
    const int received = httpd_req_recv(
        request, body + received_total, request->content_len - received_total);
    if (received <= 0) {
      std::memset(body, 0, sizeof(body));
      return setup_send(request, "request read failed", "400 Bad Request");
    }
    received_total += static_cast<size_t>(received);
  }

  char username[LocalAuth::kMaxUsernameLength + 1]{};
  char password[64]{};
  bool username_seen = false;
  bool password_seen = false;
  bool valid = true;
  char* cursor = body;
  while (valid && cursor != nullptr && *cursor != '\0') {
    char* next = std::strchr(cursor, '&');
    if (next != nullptr) *next++ = '\0';
    char* equals = std::strchr(cursor, '=');
    if (equals == nullptr) {
      valid = false;
      break;
    }
    *equals++ = '\0';
    char* destination = nullptr;
    size_t capacity = 0;
    if (std::strcmp(cursor, "username") == 0 && !username_seen) {
      destination = username;
      capacity = sizeof(username);
      username_seen = true;
    } else if (std::strcmp(cursor, "password") == 0 && !password_seen) {
      destination = password;
      capacity = sizeof(password);
      password_seen = true;
    } else {
      valid = false;
      break;
    }
    if (!decode_form_component(equals) || std::strlen(equals) >= capacity) {
      valid = false;
      break;
    }
    std::memcpy(destination, equals, std::strlen(equals) + 1);
    cursor = next;
  }
  valid = valid && username_seen && password_seen;
  esp_err_t err = valid ? context->auth->configure(username, password)
                        : ESP_ERR_INVALID_ARG;
  std::memset(password, 0, sizeof(password));
  std::memset(body, 0, sizeof(body));
  std::memset(username, 0, sizeof(username));
  if (err != ESP_OK) {
    return setup_send(request, "invalid credentials or configuration already set",
                      "400 Bad Request");
  }
  return setup_send(request,
                    "Local credentials saved. Reload this page to continue.");
}

esp_err_t LocalServer::status_handler(httpd_req_t* request) {
  auto* context = static_cast<ServerContext*>(request->user_ctx);
  if (!authorize(request, context->auth)) {
    return unauthorized(request);
  }
  char ssid[33]{};
  if (context->wifi->get_station_ssid(ssid, sizeof(ssid)) != ESP_OK) {
    std::strcpy(ssid, "unavailable");
  }
  esp_netif_ip_info_t ip_info{};
  esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  char ip[16] = "unavailable";
  if (netif != nullptr && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
    std::snprintf(ip, sizeof(ip), IPSTR, IP2STR(&ip_info.ip));
  }
  char escaped_ssid[99]{};
  size_t escaped_length = 0;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(ssid);
       *p != '\0' && escaped_length + 7 < sizeof(escaped_ssid); ++p) {
    if (*p == '"' || *p == '\\') {
      escaped_ssid[escaped_length++] = '\\';
      escaped_ssid[escaped_length++] = static_cast<char>(*p);
    } else if (*p < 0x20) {
      escaped_ssid[escaped_length++] = '?';
    } else {
      escaped_ssid[escaped_length++] = static_cast<char>(*p);
    }
  }
  char ping_target[64]{};
  context->reachability->get_target(ping_target, sizeof(ping_target));
  float temperature = 0.0f;
  float humidity = 0.0f;
  const bool sensor_available = context->environment->read(&temperature, &humidity);
  char body[1200];
  const char* reachability = "unknown";
  switch (context->reachability->state()) {
    case Reachability::kReachable:
      reachability = "reachable";
      break;
    case Reachability::kUnreachable:
      reachability = "unreachable";
      break;
    case Reachability::kUnknown:
      break;
  }
  const char* power_led_state = "low";
  switch (context->power_led->state()) {
    case PowerLedState::kHigh:
      power_led_state = "high";
      break;
    case PowerLedState::kBlinking:
      power_led_state = "blinking";
      break;
    case PowerLedState::kLow:
      break;
  }
  std::snprintf(body, sizeof(body),
                "{\"configured\":%s,\"wifi\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\",\"pingTarget\":\"%s\",\"reachability\":\"%s\",\"pingCountdown\":%u,\"sensorAvailable\":%s,\"temperature\":%.1f,\"humidity\":%.1f,\"powerLedLevel\":%s,\"powerLedState\":\"%s\",\"pcPowerState\":\"unknown\",\"cloudConfigured\":%s,\"cloudOnline\":%s}",
                context->auth->is_configured() ? "true" : "false",
                context->wifi->is_station_connected() ? "connected" : "disconnected",
                escaped_ssid, ip, ping_target, reachability,
                static_cast<unsigned>(context->reachability->seconds_until_next_probe()),
                sensor_available ? "true" : "false", temperature, humidity,
                context->power_led->level() ? "true" : "false", power_led_state,
                context->cloud->is_configured() ? "true" : "false",
                context->cloud->is_online() ? "true" : "false");
  return send_json(request, body);
}

esp_err_t LocalServer::password_handler(httpd_req_t* request) {
  auto* context = static_cast<ServerContext*>(request->user_ctx);
  if (!authorize(request, context->auth)) return unauthorized(request);
  constexpr size_t kMaxBody = 96;
  if (request->content_len <= 0 || request->content_len > kMaxBody) {
    return send_json(request, "{\"error\":\"invalid request\"}", 400);
  }
  char body[kMaxBody + 1]{};
  size_t total = 0;
  while (total < static_cast<size_t>(request->content_len)) {
    int n = httpd_req_recv(request, body + total, request->content_len - total);
    if (n <= 0) return send_json(request, "{\"error\":\"read failed\"}", 400);
    total += static_cast<size_t>(n);
  }
  if (std::strncmp(body, "password=", 9) != 0 ||
      !decode_form_component(body + 9) || std::strchr(body + 9, '&') != nullptr) {
    std::memset(body, 0, sizeof(body));
    return send_json(request, "{\"error\":\"invalid password form\"}", 400);
  }
  const esp_err_t err = context->auth->change_password(body + 9);
  std::memset(body, 0, sizeof(body));
  return err == ESP_OK ? send_json(request, "{\"status\":\"updated\"}")
                       : send_json(request, "{\"error\":\"password rejected\"}", 400);
}

esp_err_t LocalServer::wifi_handler(httpd_req_t* request) {
  auto* context = static_cast<ServerContext*>(request->user_ctx);
  if (!authorize(request, context->auth)) return unauthorized(request);
  constexpr size_t kMaxBody = 512;
  if (request->content_len <= 0 || request->content_len > kMaxBody) {
    return send_json(request, "{\"error\":\"invalid request\"}", 400);
  }
  char body[kMaxBody + 1]{};
  size_t total = 0;
  while (total < static_cast<size_t>(request->content_len)) {
    int n = httpd_req_recv(request, body + total, request->content_len - total);
    if (n <= 0) {
      std::memset(body, 0, sizeof(body));
      return send_json(request, "{\"error\":\"read failed\"}", 400);
    }
    total += static_cast<size_t>(n);
  }
  char ssid[33]{};
  char password[64]{};
  bool ssid_seen = false;
  bool password_seen = false;
  bool valid = true;
  char* cursor = body;
  while (valid && cursor != nullptr && *cursor != '\0') {
    char* next = std::strchr(cursor, '&');
    if (next != nullptr) *next++ = '\0';
    char* equals = std::strchr(cursor, '=');
    if (equals == nullptr) { valid = false; break; }
    *equals++ = '\0';
    char* destination = nullptr;
    size_t capacity = 0;
    if (std::strcmp(cursor, "ssid") == 0 && !ssid_seen) {
      destination = ssid; capacity = sizeof(ssid); ssid_seen = true;
    } else if (std::strcmp(cursor, "password") == 0 && !password_seen) {
      destination = password; capacity = sizeof(password); password_seen = true;
    } else { valid = false; break; }
    if (!decode_form_component(equals) || std::strlen(equals) >= capacity) {
      valid = false; break;
    }
    std::memcpy(destination, equals, std::strlen(equals) + 1);
    cursor = next;
  }
  valid = valid && ssid_seen && password_seen;
  const esp_err_t err = valid ? context->wifi->configure(ssid, password)
                              : ESP_ERR_INVALID_ARG;
  std::memset(password, 0, sizeof(password));
  std::memset(body, 0, sizeof(body));
  return err == ESP_OK ? send_json(request, "{\"status\":\"reconnecting\"}")
                       : send_json(request, "{\"error\":\"Wi-Fi credentials rejected\"}", 400);
}

esp_err_t LocalServer::cloud_config_handler(httpd_req_t* request) {
  auto* context = static_cast<ServerContext*>(request->user_ctx);
  if (context == nullptr || !authorize(request, context->auth)) return unauthorized(request);
  constexpr size_t kMaxBody = 512;
  if (request->content_len <= 0 || request->content_len > kMaxBody)
    return send_json(request, "{\"error\":\"invalid request\"}", 400);
  char body[kMaxBody + 1]{};
  size_t total = 0;
  while (total < static_cast<size_t>(request->content_len)) {
    const int n = httpd_req_recv(request, body + total, request->content_len - total);
    if (n <= 0) { std::memset(body, 0, sizeof(body)); return send_json(request, "{\"error\":\"read failed\"}", 400); }
    total += n;
  }
  char url[192]{}, device_id[65]{}, pairing_code[65]{};
  bool url_seen = false, id_seen = false, code_seen = false, valid = true;
  char* cursor = body;
  while (valid && cursor != nullptr && *cursor != '\0') {
    char* next = std::strchr(cursor, '&');
    if (next != nullptr) *next++ = '\0';
    char* equals = std::strchr(cursor, '=');
    if (equals == nullptr) { valid = false; break; }
    *equals++ = '\0';
    char* destination = nullptr;
    size_t capacity = 0;
    if (std::strcmp(cursor, "url") == 0 && !url_seen) { destination = url; capacity = sizeof(url); url_seen = true; }
    else if (std::strcmp(cursor, "deviceId") == 0 && !id_seen) { destination = device_id; capacity = sizeof(device_id); id_seen = true; }
    else if (std::strcmp(cursor, "pairingCode") == 0 && !code_seen) { destination = pairing_code; capacity = sizeof(pairing_code); code_seen = true; }
    else { valid = false; break; }
    if (!decode_form_component(equals) || std::strlen(equals) >= capacity) { valid = false; break; }
    std::memcpy(destination, equals, std::strlen(equals) + 1);
    cursor = next;
  }
  valid = valid && url_seen && id_seen && code_seen;
  const esp_err_t err = valid ? CloudClient::pair(url, device_id, pairing_code) : ESP_ERR_INVALID_ARG;
  std::memset(pairing_code, 0, sizeof(pairing_code));
  std::memset(body, 0, sizeof(body));
  std::memset(url, 0, sizeof(url));
  std::memset(device_id, 0, sizeof(device_id));
  return err == ESP_OK ? send_json(request, "{\"status\":\"paired\"}")
                       : send_json(request, "{\"error\":\"pairing failed; check URL, device ID, code, and network\"}", 400);
}

esp_err_t LocalServer::ping_handler(httpd_req_t* request) {
  auto* context = static_cast<ServerContext*>(request->user_ctx);
  if (!authorize(request, context->auth)) return unauthorized(request);
  constexpr size_t kMaxBody = 32;
  if (request->content_len <= 0 || request->content_len > kMaxBody) {
    return send_json(request, "{\"error\":\"invalid request\"}", 400);
  }
  char body[kMaxBody + 1]{};
  size_t total = 0;
  while (total < static_cast<size_t>(request->content_len)) {
    const int n = httpd_req_recv(request, body + total, request->content_len - total);
    if (n <= 0) return send_json(request, "{\"error\":\"read failed\"}", 400);
    total += static_cast<size_t>(n);
  }
  if (std::strncmp(body, "ip=", 3) != 0 || !decode_form_component(body + 3) ||
      std::strchr(body + 3, '&') != nullptr || std::strlen(body + 3) > 15) {
    std::memset(body, 0, sizeof(body));
    return send_json(request, "{\"error\":\"invalid IPv4 address\"}", 400);
  }
  in_addr address{};
  const bool valid = inet_aton(body + 3, &address) != 0;
  const esp_err_t err = valid ? context->reachability->configure_target(body + 3)
                              : ESP_ERR_INVALID_ARG;
  std::memset(body, 0, sizeof(body));
  return err == ESP_OK ? send_json(request, "{\"status\":\"testing\"}")
                       : send_json(request, "{\"error\":\"invalid IPv4 address\"}", 400);
}

esp_err_t LocalServer::power_press_handler(httpd_req_t* request) {
  auto* context = static_cast<ServerContext*>(request->user_ctx);
  if (!authorize(request, context->auth)) {
    return unauthorized(request);
  }

  char command_id[37];
  make_command_id(command_id);
  const PressResult result = context->actuator->request_press(command_id);

  switch (result) {
    case PressResult::kAccepted: {
      char body[128];
      std::snprintf(body, sizeof(body),
                    "{\"status\":\"accepted\",\"commandId\":\"%s\"}",
                    command_id);
      return send_json(request, body);
    }
    case PressResult::kDuplicate:
      return send_json(request, "{\"status\":\"duplicate\"}", 400);
    case PressResult::kBusy:
      return send_json(request, "{\"status\":\"busy\"}", 400);
    case PressResult::kInvalidCommand:
      return send_json(request, "{\"status\":\"invalid\"}", 400);
  }
  return send_json(request, "{\"status\":\"failed\"}", 400);
}

esp_err_t LocalServer::unauthorized(httpd_req_t* request) {
  httpd_resp_set_status(request, "401 Unauthorized");
  httpd_resp_set_hdr(request, "WWW-Authenticate", "Basic realm=local");
  return httpd_resp_send(request, "{\"error\":\"unauthorized\"}",
                         HTTPD_RESP_USE_STRLEN);
}

bool LocalServer::authorize(httpd_req_t* request, LocalAuth* auth) {
  return auth != nullptr && auth->authenticate(request);
}

void LocalServer::make_command_id(char output[37]) {
  const uint32_t a = esp_random();
  const uint32_t b = esp_random();
  const uint32_t c = esp_random();
  const uint32_t d = esp_random();
  std::snprintf(output, 37, "%08lx-%04lx-4%03lx-8%03lx-%08lx%04lx",
                static_cast<unsigned long>(a),
                static_cast<unsigned long>((b >> 16) & 0xffff),
                static_cast<unsigned long>(c & 0x0fff),
                static_cast<unsigned long>(d & 0x0fff),
                static_cast<unsigned long>(d >> 12),
                static_cast<unsigned long>(b & 0xffff));
}

}  // namespace pc_power
