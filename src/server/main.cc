#include <exception>
#include <filesystem>
#include <iostream>

#include "server/service-config.h"
#include "server/service-runner.h"
#ifdef _WIN32
#include "server/windows-service.h"
#endif

int main(int argc, char **argv) {
  try {
    auto overrides = hr_standalone::ParseServiceOverrides(argc, argv);
#ifdef _WIN32
    if (overrides.service_mode) {
      return hr_standalone::RunWindowsService(overrides);
    }
#else
    if (overrides.service_mode) {
      std::cerr << "--service is only supported on Windows\n";
      return 2;
    }
#endif
    auto config_path = overrides.config.value_or("config/service.json");
    auto config = hr_standalone::LoadServiceConfig(config_path, overrides);
    hr_standalone::StopController stop;
    hr_standalone::InstallForegroundStopHandlers(&stop);
    return hr_standalone::RunService(config, stop);
  } catch (const std::exception &error) {
    std::cerr << "Service startup failed: " << error.what() << "\n";
    return 2;
  }
}
