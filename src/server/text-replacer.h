#ifndef HR_STANDALONE_SERVER_TEXT_REPLACER_H_
#define HR_STANDALONE_SERVER_TEXT_REPLACER_H_

#include <functional>
#include <memory>
#include <string>

#include "server/service-config.h"

namespace hr_standalone {

class TextReplacer {
 public:
  virtual ~TextReplacer() = default;
  virtual std::string Replace(const std::string &text) = 0;
};

using TextReplacerFactory =
    std::function<std::unique_ptr<TextReplacer>()>;

TextReplacerFactory CreateHomophoneReplacerFactory(
    const ServiceConfig &config);

}  // namespace hr_standalone

#endif  // HR_STANDALONE_SERVER_TEXT_REPLACER_H_
