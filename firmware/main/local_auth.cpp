#include "local_auth.hpp"

#include <cstring>

#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace pc_power {
namespace {
constexpr char kTag[] = "local-auth";
constexpr char kNamespace[] = "local_auth";
constexpr char kUsernameKey[] = "username";
constexpr char kPasswordHashKey[] = "password_hash";
constexpr char kPasswordSaltKey[] = "password_salt";
constexpr int kPasswordIterations = 10000;
constexpr char kBasicPrefix[] = "Basic ";
constexpr size_t kDecodedCredentialsMax =
    LocalAuth::kMaxUsernameLength + 1 + 64;

bool hash_password(const char* password,
                   const uint8_t salt[LocalAuth::kPasswordSaltLength],
                   uint8_t output[LocalAuth::kPasswordHashLength]) {
  return mbedtls_pkcs5_pbkdf2_hmac_ext(
             MBEDTLS_MD_SHA256,
             reinterpret_cast<const unsigned char*>(password),
             std::strlen(password), salt, LocalAuth::kPasswordSaltLength,
             kPasswordIterations, LocalAuth::kPasswordHashLength, output) == 0;
}

bool constant_time_equal(const uint8_t* left, const uint8_t* right,
                         size_t length) {
  uint8_t difference = 0;
  for (size_t i = 0; i < length; ++i) {
    difference |= left[i] ^ right[i];
  }
  return difference == 0;
}
}  // namespace

esp_err_t LocalAuth::begin() {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    return ESP_OK;
  }
  if (err != ESP_OK) {
    return err;
  }

  size_t username_length = sizeof(username_);
  err = nvs_get_str(handle, kUsernameKey, username_, &username_length);
  if (err != ESP_OK || username_length <= 1) {
    nvs_close(handle);
    return ESP_OK;
  }

  size_t hash_length = sizeof(password_hash_);
  err = nvs_get_blob(handle, kPasswordHashKey, password_hash_, &hash_length);
  size_t salt_length = sizeof(password_salt_);
  if (err == ESP_OK) {
    err = nvs_get_blob(handle, kPasswordSaltKey, password_salt_, &salt_length);
  }
  nvs_close(handle);
  configured_ = err == ESP_OK && hash_length == sizeof(password_hash_) &&
                salt_length == sizeof(password_salt_);
  if (configured_) {
    ESP_LOGI(kTag, "local credentials loaded");
  }
  return ESP_OK;
}

esp_err_t LocalAuth::configure(const char* username, const char* password) {
  if (username == nullptr || password == nullptr || username[0] == '\0' ||
      std::strlen(username) > kMaxUsernameLength || std::strlen(password) < 12) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t password_hash[kPasswordHashLength];
  uint8_t password_salt[kPasswordSaltLength];
  for (uint8_t& byte : password_salt) byte = static_cast<uint8_t>(esp_random());
  if (!hash_password(password, password_salt, password_hash)) {
    return ESP_FAIL;
  }

  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return err;
  }
  err = nvs_set_str(handle, kUsernameKey, username);
  if (err == ESP_OK) {
    err = nvs_set_blob(handle, kPasswordHashKey, password_hash,
                       sizeof(password_hash));
  }
  if (err == ESP_OK) {
    err = nvs_set_blob(handle, kPasswordSaltKey, password_salt,
                       sizeof(password_salt));
  }
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  if (err != ESP_OK) {
    return err;
  }

  std::strncpy(username_, username, sizeof(username_) - 1);
  std::memcpy(password_hash_, password_hash, sizeof(password_hash_));
  std::memcpy(password_salt_, password_salt, sizeof(password_salt_));
  configured_ = true;
  ESP_LOGI(kTag, "local credentials configured");
  return ESP_OK;
}

esp_err_t LocalAuth::change_password(const char* password) {
  if (!configured_ || password == nullptr || std::strlen(password) < 12 ||
      std::strlen(password) > 63) {
    return ESP_ERR_INVALID_ARG;
  }
  uint8_t hash[kPasswordHashLength];
  uint8_t salt[kPasswordSaltLength];
  for (uint8_t& byte : salt) byte = static_cast<uint8_t>(esp_random());
  if (!hash_password(password, salt, hash)) return ESP_FAIL;

  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) return err;
  err = nvs_set_blob(handle, kPasswordHashKey, hash, sizeof(hash));
  if (err == ESP_OK) err = nvs_set_blob(handle, kPasswordSaltKey, salt, sizeof(salt));
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  if (err == ESP_OK) {
    std::memcpy(password_hash_, hash, sizeof(password_hash_));
    std::memcpy(password_salt_, salt, sizeof(password_salt_));
  }
  std::memset(hash, 0, sizeof(hash));
  std::memset(salt, 0, sizeof(salt));
  return err;
}

bool LocalAuth::authenticate(httpd_req_t* request) const {
  if (!configured_ || request == nullptr) {
    return false;
  }

  size_t header_length = httpd_req_get_hdr_value_len(request, "Authorization");
  if (header_length == 0 || header_length >= 96) {
    return false;
  }

  char header[96]{};
  if (httpd_req_get_hdr_value_str(request, "Authorization", header,
                                  sizeof(header)) != ESP_OK ||
      std::strncmp(header, kBasicPrefix, sizeof(kBasicPrefix) - 1) != 0) {
    return false;
  }

  const char* encoded = header + sizeof(kBasicPrefix) - 1;
  uint8_t decoded[kDecodedCredentialsMax]{};
  size_t decoded_length = 0;
  if (mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &decoded_length,
                            reinterpret_cast<const uint8_t*>(encoded),
                            std::strlen(encoded)) != 0) {
    return false;
  }
  decoded[decoded_length] = '\0';

  const char* separator = reinterpret_cast<const char*>(decoded);
  const char* password = std::strchr(separator, ':');
  if (password == nullptr ||
      std::strncmp(separator, username_, password - separator) != 0 ||
      static_cast<size_t>(password - separator) != std::strlen(username_)) {
    return false;
  }

  uint8_t candidate_hash[kPasswordHashLength];
  if (!hash_password(password + 1, password_salt_, candidate_hash)) {
    return false;
  }
  return constant_time_equal(candidate_hash, password_hash_,
                             sizeof(password_hash_));
}

}  // namespace pc_power
