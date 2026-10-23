#include "maintenance_console.hpp"

#include <cstdio>
#include <cstring>

#include "argtable3/argtable3.h"
#include "esp_check.h"
#include "esp_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

namespace pc_power {
namespace {
constexpr char kTag[] = "maintenance";
LocalAuth* g_auth = nullptr;
WifiManager* g_wifi = nullptr;
ReachabilityMonitor* g_reachability = nullptr;

esp_err_t read_hidden(const char* prompt, char* output, size_t capacity) {
  if (output == nullptr || capacity < 2) return ESP_ERR_INVALID_ARG;
  std::printf("%s", prompt);
  std::fflush(stdout);
  size_t length = 0;
  while (length + 1 < capacity) {
    const int byte = std::getchar();
    if (byte == EOF) continue;
    if (byte == '\r') continue;
    if (byte == '\n') break;
    if (byte == 0x03) {
      output[0] = '\0';
      std::printf("\ninput cancelled\n");
      return ESP_ERR_INVALID_STATE;
    }
    if (byte == 0x08 || byte == 0x7f) {
      if (length > 0) --length;
      continue;
    }
    if (byte >= 0x20 && byte <= 0x7e) output[length++] = static_cast<char>(byte);
  }
  output[length] = '\0';
  std::printf("\n");
  return ESP_OK;
}

void clear_secret(char* value, size_t capacity) {
  if (value != nullptr) std::memset(value, 0, capacity);
}

int config_command(int argc, char**) {
  if (argc != 1 || g_auth == nullptr) {
    std::fprintf(stderr, "usage: local-config\n");
    return 2;
  }
  if (g_auth->is_configured()) {
    std::fprintf(stderr, "already configured; credentials are immutable in field use\n");
    return 3;
  }
  char username[LocalAuth::kMaxUsernameLength + 1]{};
  char password[65]{};
  esp_err_t err = read_hidden("username: ", username, sizeof(username));
  if (err == ESP_OK) err = read_hidden("password (12+ chars): ", password, sizeof(password));
  if (err == ESP_OK) err = g_auth->configure(username, password);
  clear_secret(password, sizeof(password));
  clear_secret(username, sizeof(username));
  if (err != ESP_OK) {
    std::fprintf(stderr, "local credential configuration failed: %s\n", esp_err_to_name(err));
    return 1;
  }
  std::printf("local credentials configured\n");
  return 0;
}

int ping_target_command(int argc, char** argv) {
  if (argc != 2 || g_reachability == nullptr) {
    std::fprintf(stderr, "usage: ping-target <ipv4-or-hostname>\n");
    return 2;
  }
  const esp_err_t err = g_reachability->configure_target(argv[1]);
  if (err != ESP_OK) {
    std::fprintf(stderr, "ping target configuration failed: %s\n",
                 esp_err_to_name(err));
    return 1;
  }
  std::printf("ping target configured\n");
  return 0;
}

int wifi_config_command(int argc, char**) {
  if (argc != 1 || g_wifi == nullptr) {
    std::fprintf(stderr, "usage: wifi-config\n");
    return 2;
  }
  char ssid[33]{};
  char password[64]{};
  esp_err_t err = read_hidden("Wi-Fi SSID: ", ssid, sizeof(ssid));
  if (err == ESP_OK) err = read_hidden("Wi-Fi password: ", password, sizeof(password));
  if (err == ESP_OK) err = g_wifi->configure(ssid, password);
  clear_secret(password, sizeof(password));
  clear_secret(ssid, sizeof(ssid));
  if (err != ESP_OK) {
    std::fprintf(stderr, "Wi-Fi configuration failed: %s\n", esp_err_to_name(err));
    return 1;
  }
  std::printf("Wi-Fi credentials stored; connection attempt started\n");
  return 0;
}

int status_command(int, char**) {
  if (g_auth == nullptr) {
    return 1;
  }
  std::printf("local credentials: %s\n",
              g_auth->is_configured() ? "configured" : "not-configured");
  return 0;
}
}  // namespace

esp_err_t MaintenanceConsole::start(LocalAuth* auth, WifiManager* wifi,
                                     ReachabilityMonitor* reachability) {
  if (auth == nullptr || wifi == nullptr || reachability == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  g_auth = auth;
  g_wifi = wifi;
  g_reachability = reachability;

  esp_console_config_t console_config = ESP_CONSOLE_CONFIG_DEFAULT();
  ESP_RETURN_ON_ERROR(esp_console_init(&console_config), kTag,
                      "initialize console failed");

  esp_console_cmd_t status_cmd{};
  status_cmd.command = "local-status";
  status_cmd.help = "show local credential state";
  status_cmd.func = &status_command;
  ESP_RETURN_ON_ERROR(esp_console_cmd_register(&status_cmd), kTag,
                      "register local-status failed");

  esp_console_cmd_t config_cmd{};
  config_cmd.command = "local-config";
  config_cmd.help = "set local credentials with hidden USB input";
  config_cmd.func = &config_command;
  ESP_RETURN_ON_ERROR(esp_console_cmd_register(&config_cmd), kTag,
                      "register local-config failed");

  esp_console_cmd_t wifi_cmd{};
  wifi_cmd.command = "wifi-config";
  wifi_cmd.help = "store Wi-Fi credentials with hidden USB input";
  wifi_cmd.func = &wifi_config_command;
  ESP_RETURN_ON_ERROR(esp_console_cmd_register(&wifi_cmd), kTag,
                      "register wifi-config failed");

  esp_console_cmd_t ping_cmd{};
  ping_cmd.command = "ping-target";
  ping_cmd.help = "configure IPv4 address or hostname for reachability checks";
  ping_cmd.hint = "<ipv4-or-hostname>";
  ping_cmd.func = &ping_target_command;
  ESP_RETURN_ON_ERROR(esp_console_cmd_register(&ping_cmd), kTag,
                      "register ping-target failed");

  ESP_LOGI(kTag, "maintenance commands registered on system USB Serial/JTAG console");
  return ESP_OK;
}

}  // namespace pc_power
