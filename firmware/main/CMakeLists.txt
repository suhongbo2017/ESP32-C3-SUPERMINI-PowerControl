idf_component_register(
  SRCS "main.cpp" "power_actuator.cpp" "local_auth.cpp" "local_server.cpp" "maintenance_console.cpp" "wifi_manager.cpp" "reachability_monitor.cpp" "environment_sensor.cpp" "power_led_monitor.cpp" "cloud_client.cpp"
  INCLUDE_DIRS "."
  REQUIRES driver esp_event esp_http_client esp_http_server esp_netif esp_wifi lwip mbedtls nvs_flash console
)
