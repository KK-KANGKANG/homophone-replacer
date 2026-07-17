#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "server/service-api.h"

using hr_standalone::ReplacerPool;
using hr_standalone::ServiceApi;
using hr_standalone::ServiceConfig;
using hr_standalone::TextReplacer;

#define CHECK(condition)                                                \
  do {                                                                  \
    if (!(condition)) {                                                 \
      std::cerr << "Check failed at line " << __LINE__ << ": "       \
                << #condition << std::endl;                             \
      return 1;                                                         \
    }                                                                   \
  } while (false)

class FakeReplacer final : public TextReplacer {
 public:
  std::string Replace(const std::string &text) override {
    if (text == "throw") throw std::runtime_error("failure");
    return text == "左侧乱潮" ? "左侧卵巢" : text;
  }
};

int main() {
  ReplacerPool pool(1, 2, [] { return std::make_unique<FakeReplacer>(); });
  ServiceConfig config;
  config.server.max_text_characters = 4;
  ServiceApi api(pool, config, true, true, false);

  auto ok = api.Replace(R"({"text":"左侧乱潮"})");
  CHECK(ok.status == 200);
  auto body = nlohmann::json::parse(ok.body);
  CHECK(body["code"] == 0);
  CHECK(body["result"] == "左侧卵巢");
  CHECK(body["processing_ms"].is_number());

  CHECK(api.Replace("{").status == 400);
  CHECK(api.Replace(R"({})").status == 400);
  CHECK(api.Replace(R"({"text":2})").status == 400);
  CHECK(api.Replace(R"({"text":""})").status == 400);
  CHECK(api.Replace(R"({"text":"一二三四五"})").status == 413);
  CHECK(api.Replace(R"({"text":"throw"})").status == 413);

  auto health = nlohmann::json::parse(api.Health().body);
  CHECK(health["status"] == "ok");
  CHECK(health["rules"]["fst_loaded"] == true);
  CHECK(health["rules"]["mapping_enabled"] == false);

  api.SetStopping();
  CHECK(api.Replace(R"({"text":"正常"})").status == 503);
  pool.Stop(std::chrono::seconds(1));

  ReplacerPool failing(1, 2,
                       [] { return std::make_unique<FakeReplacer>(); });
  config.server.max_text_characters = 20;
  ServiceApi failing_api(failing, config, true, true, false);
  CHECK(failing_api.Replace(R"({"text":"throw"})").status == 500);
  failing.Stop(std::chrono::seconds(1));
  return 0;
}
