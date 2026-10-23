#include "environment_sensor.hpp"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace pc_power {
namespace {
constexpr char kTag[] = "environment";
constexpr uint8_t kAddress = 0x38;
constexpr gpio_num_t kSda = GPIO_NUM_4;
constexpr gpio_num_t kScl = GPIO_NUM_5;
constexpr TickType_t kInterval = pdMS_TO_TICKS(10000);

}  // namespace

esp_err_t EnvironmentSensor::start() {
  i2c_master_bus_config_t bus_config{};
  bus_config.i2c_port = I2C_NUM_0;
  bus_config.sda_io_num = kSda;
  bus_config.scl_io_num = kScl;
  bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_config.glitch_ignore_cnt = 7;
  bus_config.flags.enable_internal_pullup = true;
  esp_err_t err = i2c_new_master_bus(&bus_config, &bus_);
  if (err != ESP_OK) return err;

  i2c_device_config_t device_config{};
  device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  device_config.device_address = kAddress;
  device_config.scl_speed_hz = 100000;
  err = i2c_master_bus_add_device(bus_, &device_config, &device_);
  if (err != ESP_OK) return err;

  const uint8_t initialize[] = {0xBE, 0x08, 0x00};
  err = i2c_master_transmit(device_, initialize, sizeof(initialize), 100);
  if (err != ESP_OK) return err;
  vTaskDelay(pdMS_TO_TICKS(20));

  if (xTaskCreate(&task_entry, "environment", 3072, this, 3, nullptr) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  ESP_LOGI(kTag, "AHT25 initialized on SDA=GPIO4 SCL=GPIO5 address=0x38");
  return ESP_OK;
}

bool EnvironmentSensor::read_once(float* temperature_c, float* humidity_percent) const {
  const uint8_t command[] = {0xAC, 0x33, 0x00};
  if (i2c_master_transmit(device_, command, sizeof(command), 100) != ESP_OK) {
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(80));
  uint8_t data[7]{};
  if (i2c_master_receive(device_, data, sizeof(data), 100) != ESP_OK ||
      (data[0] & 0x80) != 0) {
    return false;
  }

  const uint32_t raw_humidity = (static_cast<uint32_t>(data[1]) << 12) |
                                (static_cast<uint32_t>(data[2]) << 4) |
                                (data[3] >> 4);
  const uint32_t raw_temperature =
      (static_cast<uint32_t>(data[3] & 0x0f) << 16) |
      (static_cast<uint32_t>(data[4]) << 8) | data[5];
  *humidity_percent = 100.0f * raw_humidity / 1048576.0f;
  *temperature_c = -50.0f + 200.0f * raw_temperature / 1048576.0f;
  return true;
}

bool EnvironmentSensor::read(float* temperature_c, float* humidity_percent) const {
  if (temperature_c == nullptr || humidity_percent == nullptr ||
      !available_.load()) return false;
  std::lock_guard<std::mutex> lock(lock_);
  *temperature_c = temperature_c_;
  *humidity_percent = humidity_percent_;
  return true;
}

void EnvironmentSensor::task_entry(void* arg) {
  static_cast<EnvironmentSensor*>(arg)->task_loop();
}

void EnvironmentSensor::task_loop() {
  while (true) {
    float temperature = 0.0f;
    float humidity = 0.0f;
    if (read_once(&temperature, &humidity)) {
      {
        std::lock_guard<std::mutex> lock(lock_);
        temperature_c_ = temperature;
        humidity_percent_ = humidity;
      }
      available_.store(true);
    } else {
      available_.store(false);
      ESP_LOGW(kTag, "AHT25 read failed");
    }
    vTaskDelay(kInterval);
  }
}

}  // namespace pc_power
