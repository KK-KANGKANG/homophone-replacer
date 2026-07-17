#ifndef HR_STANDALONE_SERVER_SERVICE_CONFIG_H_
#define HR_STANDALONE_SERVER_SERVICE_CONFIG_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace hr_standalone {

struct ServerSettings {
  std::string host = "0.0.0.0";
  uint16_t port = 18080;
  size_t worker_count = 2;
  size_t queue_capacity = 100;
  size_t max_text_characters = 10000;
};

struct RuleSettings {
  std::filesystem::path lexicon = "data/hr-files/lexicon.txt";
  std::filesystem::path fst = "data/hr-files/replace.fst";
  bool mapping_enabled = false;
  std::filesystem::path mapping = "data/hr-files/mapping.txt";
};

struct LoggingSettings {
  std::string level = "info";
  std::filesystem::path directory = "logs";
  size_t max_file_mb = 50;
  size_t max_files = 10;
};

struct ServiceConfig {
  std::filesystem::path root;
  ServerSettings server;
  RuleSettings rules;
  LoggingSettings logging;
};

struct ServiceOverrides {
  std::optional<std::filesystem::path> config;
  std::optional<std::string> host;
  std::optional<uint16_t> port;
  std::optional<std::filesystem::path> mapping;
  bool service_mode = false;
};

std::filesystem::path ResolveFromRoot(
    const std::filesystem::path &root,
    const std::filesystem::path &value);
ServiceConfig LoadServiceConfig(const std::filesystem::path &path,
                                const ServiceOverrides &overrides);
ServiceOverrides ParseServiceOverrides(int argc, char **argv);
std::string ServiceUsage();

}  // namespace hr_standalone

#endif  // HR_STANDALONE_SERVER_SERVICE_CONFIG_H_
