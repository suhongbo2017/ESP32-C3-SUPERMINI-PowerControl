#include <cstdlib>

#include "control_policy.hpp"

int main() {
  using pc_power::local_control_available;

  if (local_control_available(false, false)) return EXIT_FAILURE;
  if (local_control_available(false, true)) return EXIT_FAILURE;
  if (local_control_available(true, false)) return EXIT_FAILURE;
  if (!local_control_available(true, true)) return EXIT_FAILURE;
  return EXIT_SUCCESS;
}
