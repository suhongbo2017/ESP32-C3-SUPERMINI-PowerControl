#pragma once

namespace pc_power {

constexpr bool local_control_available(bool station_connected,
                                       bool credentials_configured) {
  return station_connected && credentials_configured;
}

}  // namespace pc_power
