#include "server/service-runner.h"

#include <chrono>
#include <csignal>
#include <filesystem>
#include <memory>
#include <thread>
#include <vector>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "server/http-server.h"
#include "server/replacer-pool.h"
#include "server/service-api.h"
#include "server/text-replacer.h"

namespace hr_standalone {
namespace {

StopController *g_stop = nullptr;

void SignalHandler(int) {
  if (g_stop) g_stop->RequestStop();
}

#ifdef _WIN32
BOOL WINAPI ConsoleHandler(DWORD event) {
  if (event == CTRL_C_EVENT || event == CTRL_CLOSE_EVENT ||
      event == CTRL_BREAK_EVENT) {
    if (g_stop) g_stop->RequestStop();
    return TRUE;
  }
  return FALSE;
}
#endif

void InitializeLogging(const LoggingSettings &settings) {
  std::filesystem::create_directories(settings.directory);
  auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
      (settings.directory / "service.log").string(),
      settings.max_file_mb * 1024 * 1024, settings.max_files);
  auto logger = std::make_shared<spdlog::logger>(
      "service", spdlog::sinks_init_list{console, file});
  logger->set_level(spdlog::level::from_str(settings.level));
  spdlog::set_default_logger(std::move(logger));
}

}  // namespace

void StopController::RequestStop() { stopping_ = true; }
bool StopController::IsStopping() const { return stopping_; }

int RunService(const ServiceConfig &config, StopController &stop) {
  InitializeLogging(config.logging);
  const bool mapping_loaded = config.rules.mapping_enabled &&
                              std::filesystem::exists(config.rules.mapping);
  ReplacerPool pool(config.server.worker_count, config.server.queue_capacity,
                    CreateHomophoneReplacerFactory(config));
  ServiceApi api(pool, config, true, true, mapping_loaded);
  HttpServer server(api, config);
  std::thread stopper([&] {
    while (!stop.IsStopping()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    api.SetStopping();
    server.Stop();
  });
  spdlog::info("service starting host={} port={} workers={}",
               config.server.host, config.server.port,
               config.server.worker_count);
  bool listened = server.Listen();
  if (!stop.IsStopping()) stop.RequestStop();
  if (stopper.joinable()) stopper.join();
  pool.Stop(std::chrono::seconds(10));
  spdlog::info("service stopped");
  return listened ? 0 : 1;
}

void InstallForegroundStopHandlers(StopController *stop) {
  g_stop = stop;
#ifdef _WIN32
  SetConsoleCtrlHandler(ConsoleHandler, TRUE);
#else
  std::signal(SIGINT, SignalHandler);
  std::signal(SIGTERM, SignalHandler);
#endif
}

}  // namespace hr_standalone
