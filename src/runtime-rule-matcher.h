#ifndef RUNTIME_RULE_MATCHER_H_
#define RUNTIME_RULE_MATCHER_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hr_standalone {

struct RuntimeRuleMatch {
  size_t end_index;
  std::string replacement;
  bool exact;
};

std::string RemoveToneDigits(std::string_view pinyin);

class RuntimeRuleMatcher {
 public:
  void Reset(const std::unordered_map<std::string, std::string> &rules);
  std::optional<RuntimeRuleMatch> FindBest(
      const std::vector<std::string> &pronunciations,
      size_t begin_index) const;

 private:
  bool HasBlockedPrefix(const std::vector<std::string> &pronunciations,
                        size_t begin_index) const;

  std::unordered_map<std::string, std::string> exact_rules_;
  std::unordered_map<std::string, std::string> flexible_rules_;
  std::unordered_set<std::string> blocked_flexible_keys_;
  size_t max_exact_key_length_ = 0;
  size_t max_flexible_key_length_ = 0;
  size_t max_blocked_key_length_ = 0;
};

}  // namespace hr_standalone

#endif  // RUNTIME_RULE_MATCHER_H_
