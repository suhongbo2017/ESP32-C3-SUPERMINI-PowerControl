#pragma once

#include <Arduino.h>

// Hardware: ESP32-C3 SuperMini, based on ESP32-C3-ST7789 board settings.
constexpr uint8_t POWER_PULSE_PIN = 1;
constexpr uint32_t POWER_PULSE_MS = 300;

// Wi-Fi station timeout before fallback AP mode.
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;

// Change this before exposing the device beyond a trusted LAN.
constexpr char OTA_USERNAME[] = "admin";
constexpr char OTA_PASSWORD[] = "change-me-now";
constexpr char AP_PASSWORD[] = "pcpower123";

// Set to true after replacing OTA_PASSWORD with a private password.
constexpr bool OTA_ENABLED = true;
