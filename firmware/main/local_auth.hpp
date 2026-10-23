#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "esp_http_server.h"

namespace pc_power {

class LocalAuth {
 public:
  static constexpr size_t kMaxUsernameLength = 32;
  static constexpr size_t kPasswordHashLength = 32;
  static constexpr size_t kPasswordSaltLength = 16;

  esp_err_t begin();
  bool is_configured() const { return configured_.load(); }
  esp_err_t configure(const char* username, const char* password);
  esp_err_t change_password(const char* password);
  bool authenticate(httpd_req_t* request) const;

 private:
  std::atomic<bool> configured_{false};
  char username_[kMaxUsernameLength + 1]{};
  uint8_t password_hash_[kPasswordHashLength]{};
  uint8_t password_salt_[kPasswordSaltLength]{};
};

}  // namespace pc_power
