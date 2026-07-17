#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "server/service-config.h"

using hr_standalone::LoadServiceConfig;
using hr_standalone::ServiceOverrides;

#define CHECK(condition)                                                \
  do {                                                                  \
    if (!(condition)) {                                                 \
      std::cerr << "Check failed at line " << __LINE__ << ": "       \
                << #condition << std::endl;                             \
      return 1;                                                         \
    }                                                                   \
  } while (false)

namespace {

class TempDirectory {
 public:
  TempDirectory() {
    path_ = std::filesystem::temp_directory_path() /
            "homophone-service-config-test";
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }

  ~TempDirectory() { std::filesystem::remove_all(path_); }

  const std::filesystem::path &path() const { return path_; }

 private:
  std::filesystem::path path_;
};

void Write(const std::filesystem::path &path, const std::string &content) {
  std::ofstream output(path);
  output << content;
}

bool ThrowsInvalid(const std::filesystem::path &path) {
  try {
    LoadServiceConfig(path, {});
  } catch (const std::invalid_argument &) {
    return true;
  }
  return false;
}

}  // namespace

int main() {
  TempDirectory temp;
  auto config_path = temp.path() / "service.json";
  Write(config_path, R"({
    "server": {
      "host": "127.0.0.1",
      "port": 19090,
      "worker_count": 3,
      "queue_capacity": 20,
      "max_text_characters": 500
    },
    "rules": {
      "lexicon": "data/lexicon.txt",
      "fst": "data/replace.fst",
      "mapping_enabled": false,
      "mapping": "data/mapping.txt"
    },
    "logging": {
      "level": "debug",
      "directory": "logs",
      "max_file_mb": 5,
      "max_files": 2
    }
  })");

  ServiceOverrides overrides;
  overrides.port = 20000;
  auto config = LoadServiceConfig(config_path, overrides);
  CHECK(config.server.host == "127.0.0.1");
  CHECK(config.server.port == 20000);
  CHECK(config.server.worker_count == 3);
  CHECK(config.server.queue_capacity == 20);
  CHECK(config.server.max_text_characters == 500);
  CHECK(!config.rules.mapping_enabled);
  CHECK(config.rules.lexicon == temp.path() / "data/lexicon.txt");

  overrides.mapping = temp.path() / "override.txt";
  config = LoadServiceConfig(config_path, overrides);
  CHECK(config.rules.mapping_enabled);
  CHECK(config.rules.mapping.filename() == "override.txt");

  Write(config_path, R"({"server":{"worker_count":0}})");
  CHECK(ThrowsInvalid(config_path));
  Write(config_path, R"({"server":{"worker_count":5}})");
  CHECK(ThrowsInvalid(config_path));
  Write(config_path, R"({"server":{"port":0}})");
  CHECK(ThrowsInvalid(config_path));
  Write(config_path, R"({"server":{"queue_capacity":0}})");
  CHECK(ThrowsInvalid(config_path));
  Write(config_path, R"({"server":{"max_text_characters":0}})");
  CHECK(ThrowsInvalid(config_path));
  return 0;
}
