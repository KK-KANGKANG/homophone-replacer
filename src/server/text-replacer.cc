#include "server/text-replacer.h"

#include <filesystem>
#include <stdexcept>
#include <utility>

#include <spdlog/spdlog.h>

#include "homophone-replacer.h"

namespace hr_standalone {
namespace {

class HomophoneTextReplacer final : public TextReplacer {
 public:
  explicit HomophoneTextReplacer(const ServiceConfig &config)
      : replacer_(CreateConfig(config)) {}

  std::string Replace(const std::string &text) override {
    return replacer_.Apply(text);
  }

 private:
  static HomophoneReplacerConfig CreateConfig(const ServiceConfig &config) {
    HomophoneReplacerConfig core(config.rules.lexicon.string(),
                                 config.rules.fst.string(), false);
    if (config.rules.mapping_enabled) {
      core.rules_file = config.rules.mapping.string();
    }
    if (!core.Validate()) {
      throw std::runtime_error("invalid homophone replacer configuration");
    }
    return core;
  }

  HomophoneReplacer replacer_;
};

}  // namespace

TextReplacerFactory CreateHomophoneReplacerFactory(
    const ServiceConfig &config) {
  ServiceConfig effective = config;
  if (effective.rules.mapping_enabled &&
      !std::filesystem::exists(effective.rules.mapping)) {
    spdlog::warn("Optional mapping file is missing; using FST only");
    effective.rules.mapping_enabled = false;
  }
  return [effective] {
    return std::make_unique<HomophoneTextReplacer>(effective);
  };
}

}  // namespace hr_standalone
