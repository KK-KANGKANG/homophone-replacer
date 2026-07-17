#include "server/service-api.h"

#include <chrono>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace hr_standalone {
namespace {

using Json = nlohmann::json;

ApiResponse Error(int status, int code, std::string message) {
  return {status, Json{{"code", code}, {"message", std::move(message)}}.dump()};
}

std::optional<size_t> Utf8Characters(std::string_view text) {
  size_t count = 0;
  for (size_t index = 0; index < text.size();) {
    uint8_t lead = static_cast<uint8_t>(text[index]);
    size_t width = 0;
    if (lead < 0x80) width = 1;
    else if ((lead & 0xE0) == 0xC0) width = 2;
    else if ((lead & 0xF0) == 0xE0) width = 3;
    else if ((lead & 0xF8) == 0xF0) width = 4;
    else return std::nullopt;
    if (index + width > text.size()) return std::nullopt;
    for (size_t offset = 1; offset < width; ++offset) {
      if ((static_cast<uint8_t>(text[index + offset]) & 0xC0) != 0x80) {
        return std::nullopt;
      }
    }
    index += width;
    ++count;
  }
  return count;
}

}  // namespace

ServiceApi::ServiceApi(ReplacerPool &pool, const ServiceConfig &config,
                       bool lexicon_loaded, bool fst_loaded,
                       bool mapping_loaded)
    : pool_(pool),
      max_text_characters_(config.server.max_text_characters),
      lexicon_loaded_(lexicon_loaded),
      fst_loaded_(fst_loaded),
      mapping_enabled_(config.rules.mapping_enabled),
      mapping_loaded_(mapping_loaded) {}

ApiResponse ServiceApi::Replace(std::string_view json_body) {
  if (stopping_) return Error(503, 50301, "service is stopping");
  Json request;
  try {
    request = Json::parse(json_body);
  } catch (const Json::exception &) {
    return Error(400, 40001, "invalid JSON body");
  }
  if (!request.contains("text") || !request["text"].is_string()) {
    return Error(400, 40002, "text is required and must be a string");
  }
  std::string text = request["text"].get<std::string>();
  if (text.empty()) return Error(400, 40003, "text cannot be empty");
  auto characters = Utf8Characters(text);
  if (!characters) return Error(400, 40004, "text must be valid UTF-8");
  if (*characters > max_text_characters_) {
    return Error(413, 41301, "text exceeds configured character limit");
  }

  auto started = std::chrono::steady_clock::now();
  auto future = pool_.Submit(std::move(text));
  if (!future) return Error(429, 42901, "request queue is full");
  try {
    auto result = future->get();
    double elapsed = std::chrono::duration<double, std::milli>(
                         std::chrono::steady_clock::now() - started)
                         .count();
    return {200, Json{{"code", 0},
                      {"message", "ok"},
                      {"result", std::move(result)},
                      {"processing_ms", elapsed}}
                     .dump()};
  } catch (const std::exception &) {
    return Error(500, 50001, "replacement failed");
  }
}

ApiResponse ServiceApi::Health() const {
  Json rules{{"lexicon_loaded", lexicon_loaded_},
             {"fst_loaded", fst_loaded_},
             {"mapping_enabled", mapping_enabled_},
             {"mapping_loaded", mapping_loaded_}};
  return {200, Json{{"status", stopping_ ? "stopping" : "ok"},
                    {"version", "1.0.0"},
                    {"rules", std::move(rules)}}
                   .dump()};
}

void ServiceApi::SetStopping() { stopping_ = true; }

}  // namespace hr_standalone
