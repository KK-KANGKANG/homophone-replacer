#include "server/service-config.h"

#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace hr_standalone {
namespace {

using Json = nlohmann::json;

template <typename T>
void ReadIfPresent(const Json &object, const char *name, T *value) {
  auto found = object.find(name);
  if (found != object.end()) *value = found->get<T>();
}

uint16_t ParsePort(const std::string &value) {
  size_t parsed = 0;
  unsigned long port = std::stoul(value, &parsed);
  if (parsed != value.size() || port == 0 ||
      port > std::numeric_limits<uint16_t>::max()) {
    throw std::invalid_argument("port must be between 1 and 65535");
  }
  return static_cast<uint16_t>(port);
}

void Validate(const ServiceConfig &config) {
  if (config.server.host.empty()) {
    throw std::invalid_argument("server.host cannot be empty");
  }
  if (config.server.port == 0) {
    throw std::invalid_argument("server.port must be between 1 and 65535");
  }
  if (config.server.worker_count < 1 || config.server.worker_count > 4) {
    throw std::invalid_argument("server.worker_count must be between 1 and 4");
  }
  if (config.server.queue_capacity == 0) {
    throw std::invalid_argument("server.queue_capacity must be positive");
  }
  if (config.server.max_text_characters == 0) {
    throw std::invalid_argument("server.max_text_characters must be positive");
  }
  if (config.logging.max_file_mb == 0 || config.logging.max_files == 0) {
    throw std::invalid_argument("logging size and file count must be positive");
  }
}

void ReadServer(const Json &root, ServerSettings *settings) {
  auto section = root.find("server");
  if (section == root.end()) return;
  ReadIfPresent(*section, "host", &settings->host);
  ReadIfPresent(*section, "port", &settings->port);
  ReadIfPresent(*section, "worker_count", &settings->worker_count);
  ReadIfPresent(*section, "queue_capacity", &settings->queue_capacity);
  ReadIfPresent(*section, "max_text_characters",
                &settings->max_text_characters);
}

void ReadRules(const Json &root, RuleSettings *settings) {
  auto section = root.find("rules");
  if (section == root.end()) return;
  ReadIfPresent(*section, "lexicon", &settings->lexicon);
  ReadIfPresent(*section, "fst", &settings->fst);
  ReadIfPresent(*section, "mapping_enabled", &settings->mapping_enabled);
  ReadIfPresent(*section, "mapping", &settings->mapping);
}

void ReadLogging(const Json &root, LoggingSettings *settings) {
  auto section = root.find("logging");
  if (section == root.end()) return;
  ReadIfPresent(*section, "level", &settings->level);
  ReadIfPresent(*section, "directory", &settings->directory);
  ReadIfPresent(*section, "max_file_mb", &settings->max_file_mb);
  ReadIfPresent(*section, "max_files", &settings->max_files);
}

const char *RequireValue(int argc, char **argv, int *index) {
  if (*index + 1 >= argc) {
    throw std::invalid_argument(std::string("missing value for ") + argv[*index]);
  }
  return argv[++(*index)];
}

std::filesystem::path ConfigRoot(const std::filesystem::path &path) {
  auto parent = std::filesystem::absolute(path).parent_path();
  return parent.filename() == "config" ? parent.parent_path() : parent;
}

}  // namespace

std::filesystem::path ResolveFromRoot(
    const std::filesystem::path &root,
    const std::filesystem::path &value) {
  return value.is_absolute() ? value.lexically_normal()
                             : (root / value).lexically_normal();
}

ServiceConfig LoadServiceConfig(const std::filesystem::path &path,
                                const ServiceOverrides &overrides) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("cannot open service config: " + path.string());
  }
  Json json;
  input >> json;
  ServiceConfig config;
  config.root = ConfigRoot(path);
  ReadServer(json, &config.server);
  ReadRules(json, &config.rules);
  ReadLogging(json, &config.logging);
  if (overrides.host) config.server.host = *overrides.host;
  if (overrides.port) config.server.port = *overrides.port;
  if (overrides.mapping) {
    config.rules.mapping = *overrides.mapping;
    config.rules.mapping_enabled = true;
  }
  config.rules.lexicon = ResolveFromRoot(config.root, config.rules.lexicon);
  config.rules.fst = ResolveFromRoot(config.root, config.rules.fst);
  config.rules.mapping = ResolveFromRoot(config.root, config.rules.mapping);
  config.logging.directory =
      ResolveFromRoot(config.root, config.logging.directory);
  Validate(config);
  return config;
}

ServiceOverrides ParseServiceOverrides(int argc, char **argv) {
  ServiceOverrides result;
  for (int index = 1; index < argc; ++index) {
    std::string argument = argv[index];
    if (argument == "--config") {
      result.config = RequireValue(argc, argv, &index);
    } else if (argument == "--host") {
      result.host = RequireValue(argc, argv, &index);
    } else if (argument == "--port") {
      result.port = ParsePort(RequireValue(argc, argv, &index));
    } else if (argument == "--mapping") {
      result.mapping = RequireValue(argc, argv, &index);
    } else if (argument == "--service") {
      result.service_mode = true;
    } else if (argument == "--help" || argument == "-h") {
      throw std::invalid_argument(ServiceUsage());
    } else {
      throw std::invalid_argument("unknown argument: " + argument + "\n" +
                                  ServiceUsage());
    }
  }
  return result;
}

std::string ServiceUsage() {
  return "homophone-replacer-server [--config FILE] [--host HOST] "
         "[--port PORT] [--mapping FILE] [--service]";
}

}  // namespace hr_standalone
