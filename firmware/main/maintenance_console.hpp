#pragma once

#include "esp_err.h"

#include "cloud_client.hpp"
#include "local_auth.hpp"
#include "reachability_monitor.hpp"
#include "wifi_manager.hpp"

namespace pc_power {

class MaintenanceConsole {
 public:
  esp_err_t start(LocalAuth* auth, WifiManager* wifi,
                  ReachabilityMonitor* reachability);
};

}  // namespace pc_power
