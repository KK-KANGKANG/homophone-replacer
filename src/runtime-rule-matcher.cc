#include "runtime-rule-matcher.h"

#include <algorithm>
#include <set>
#include <utility>

namespace hr_standalone {
namespace {

std::optional<RuntimeRuleMatch> FindLongest(
    const std::unordered_map<std::string, std::string> &rules,
    const std::vector<std::string> &pronunciations, size_t begin,
    size_t max_key_length, bool remove_tones, bool exact) {
  std::string key;
  std::vector<std::pair<size_t, std::string>> candidates;
  for (size_t end = begin; end < pronunciations.size(); ++end) {
    key += remove_tones ? RemoveToneDigits(pronunciations[end])
                        : pronunciations[end];
    if (key.size() > max_key_length) break;
    candidates.emplace_back(end + 1, key);
  }
  for (auto it = candidates.rbegin(); it != candidates.rend(); ++it) {
    auto found = rules.find(it->second);
    if (found != rules.end()) {
      return RuntimeRuleMatch{it->first, found->second, exact};
    }
  }
  return std::nullopt;
}

}  // namespace

std::string RemoveToneDigits(std::string_view pinyin) {
  std::string result;
  result.reserve(pinyin.size());
  for (char value : pinyin) {
    if (value < '1' || value > '4') result.push_back(value);
  }
  return result;
}

void RuntimeRuleMatcher::Reset(
    const std::unordered_map<std::string, std::string> &rules) {
  exact_rules_ = rules;
  flexible_rules_.clear();
  blocked_flexible_keys_.clear();
  max_exact_key_length_ = 0;
  max_flexible_key_length_ = 0;
  max_blocked_key_length_ = 0;
  std::unordered_map<std::string, std::set<std::string>> groups;
  for (const auto &entry : exact_rules_) {
    max_exact_key_length_ = std::max(max_exact_key_length_, entry.first.size());
    groups[RemoveToneDigits(entry.first)].insert(entry.second);
  }
  for (const auto &entry : groups) {
    if (entry.second.size() == 1) {
      flexible_rules_[entry.first] = *entry.second.begin();
      max_flexible_key_length_ =
          std::max(max_flexible_key_length_, entry.first.size());
    } else {
      blocked_flexible_keys_.insert(entry.first);
      max_blocked_key_length_ =
          std::max(max_blocked_key_length_, entry.first.size());
    }
  }
}

bool RuntimeRuleMatcher::HasBlockedPrefix(
    const std::vector<std::string> &pronunciations,
    size_t begin_index) const {
  std::string key;
  for (size_t end = begin_index; end < pronunciations.size(); ++end) {
    key += RemoveToneDigits(pronunciations[end]);
    if (key.size() > max_blocked_key_length_) break;
    if (blocked_flexible_keys_.count(key)) return true;
  }
  return false;
}

std::optional<RuntimeRuleMatch> RuntimeRuleMatcher::FindBest(
    const std::vector<std::string> &pronunciations,
    size_t begin_index) const {
  auto exact = FindLongest(exact_rules_, pronunciations, begin_index,
                           max_exact_key_length_, false, true);
  if (exact) return exact;
  if (HasBlockedPrefix(pronunciations, begin_index)) return std::nullopt;
  return FindLongest(flexible_rules_, pronunciations, begin_index,
                     max_flexible_key_length_, true, false);
}

}  // namespace hr_standalone
