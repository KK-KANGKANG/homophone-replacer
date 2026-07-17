// homophone-replacer.cc
//
// Copyright (c)  2025  Xiaomi Corporation

#include "homophone-replacer.h"
#include "runtime-rule-matcher.h"
#include "utils/file-utils.h"
#include "utils/text-utils.h"

#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <iostream>
#include <vector>
#include <iomanip>
#include <set>
#include <algorithm>
#include <chrono>

// 引入 FST 文本归一化器
#include "kaldifst/csrc/text-normalizer.h"

namespace hr_standalone {

bool HomophoneReplacerConfig::Validate() const {
  if (!lexicon.empty() && !FileExists(lexicon)) {
    std::cerr << "Error: lexicon file '" << lexicon << "' does not exist" << std::endl;
    return false;
  }

  if (!rule_fsts.empty()) {
    std::vector<std::string> files;
    SplitStringToVector(rule_fsts, ",", false, &files);

    if (files.size() > 1) {
      std::cerr << "Error: Only 1 FST file is supported now." << std::endl;
      return false;
    }

    for (const auto &f : files) {
      if (!FileExists(f)) {
        std::cerr << "Error: Rule fst '" << f << "' does not exist." << std::endl;
        return false;
      }
    }
  }

  return true;
}

std::string HomophoneReplacerConfig::ToString() const {
  std::ostringstream os;
  os << "HomophoneReplacerConfig(";
  os << "lexicon=\"" << lexicon << "\", ";
  os << "rule_fsts=\"" << rule_fsts << "\")";
  return os.str();
}

class HomophoneReplacer::Impl {
 public:
  explicit Impl(const HomophoneReplacerConfig &config) : config_(config) {
    if (!config.lexicon.empty()) {
      std::ifstream is(config.lexicon);
      InitLexicon(is);
    }

    // 加载 FST 规则
    if (!config.rule_fsts.empty()) {
      std::vector<std::string> files;
      SplitStringToVector(config.rule_fsts, ",", false, &files);
      replacer_list_.reserve(files.size());
      for (const auto &f : files) {
        if (config.debug) {
          std::cout << "hr rule fst: " << f << std::endl;
        }
        replacer_list_.push_back(std::make_unique<kaldifst::TextNormalizer>(f));
      }
    }

    // 应用运行时增删规则（在已有 FST 基础上额外覆盖/屏蔽）
    BuildRuntimeRuleMap();
  }

  std::string Apply(const std::string &text) const {
    if (text.empty()) {
      return text;
    }

    auto now = [](){ return std::chrono::steady_clock::now(); };
    auto t0 = now();

    // 1. 先按 UTF-8 字符切分
    std::vector<std::string> utf8_chars = SplitUtf8(text);
    
    // 2. 基于词典进行最大前向匹配分词 (替代 jieba)
    std::vector<std::string> words = Segment(utf8_chars);

    auto t1 = now();

    if (config_.debug) {
      std::cout << "Input text: '" << text << "'" << std::endl;
      std::ostringstream os;
      os << "After segmentation: ";
      std::string sep;
      for (const auto &w : words) {
        os << sep << w;
        sep = "_";
      }
      std::cout << os.str() << std::endl;
    }

    // 将连续中文片段聚合，按段应用 FST 规则
    std::string result;
    std::vector<std::string> current_words;
    std::vector<std::string> current_pronunciations;

    auto flush_segment = [&](std::string &out) {
      if (current_words.empty()) return;
      out += ApplyImpl(current_words, current_pronunciations);
      current_words.clear();
      current_pronunciations.clear();
    };

    auto t2 = now();

    for (const auto &w : words) {
      // 非中文（或长度不足一个中文字符）作为分隔，先冲刷累计段
      if (w.size() < 3 || reinterpret_cast<const uint8_t *>(w.data())[0] < 128) {
        flush_segment(result);
        result += w;
        continue;
      }

      std::string p = ConvertWordToPronunciation(w);
      std::vector<std::string> sub_chars = SplitUtf8(w);

      if (sub_chars.size() > 1) {
        // 尝试按数字（声调）拆分词组拼音
        std::vector<std::string> sub_prons;
        size_t start_pos = 0;
        for (size_t i = 0; i < p.size(); ++i) {
          if (p[i] >= '0' && p[i] <= '9') {
            sub_prons.push_back(p.substr(start_pos, i - start_pos + 1));
            start_pos = i + 1;
          }
        }

        // 如果拆分出的拼音数量与字数一致，则按字提交给 FST
        if (sub_prons.size() == sub_chars.size()) {
          for (size_t i = 0; i < sub_chars.size(); ++i) {
            current_words.push_back(sub_chars[i]);
            current_pronunciations.push_back(sub_prons[i]);
          }
          if (config_.debug) {
            std::cout << w << " (Split) -> " << p << std::endl;
          }
          continue;
        }
      }

      if (config_.debug) {
        std::cout << w << " -> " << p << std::endl;
      }
      current_words.push_back(w);
      current_pronunciations.push_back(std::move(p));
    }

    auto t3 = now();

    flush_segment(result);

    // 在整句级别应用运行时词典覆盖（最后一步，优先级最高）
    if (!runtime_rule_map_.empty()) {
      result = ApplyRuntimeOverrides(result);
    }

    auto t4 = now();

    if (config_.debug) {
      auto ms = [](auto a, auto b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
      };
      std::cout << std::fixed << std::setprecision(4)
                << "Timing(ms): seg=" << ms(t0, t1)
                << ", prep=" << ms(t1, t2)
                << ", pinyin=" << ms(t2, t3)
                << ", fst=" << ms(t3, t4)
                << std::endl;
      std::cout << "Output text: '" << result << "'" << std::endl;
    }

    return RemoveInvalidUtf8Sequences(result);
  }

 private:
  // 最大前向匹配分词
  std::vector<std::string> Segment(const std::vector<std::string>& chars) const {
    std::vector<std::string> words;
    int32_t n = static_cast<int32_t>(chars.size());
    int32_t max_len = 10; // 最大匹配长度
    
    for (int32_t i = 0; i < n; ) {
      bool matched = false;
      // 如果是字母或数字，合并连续的
      if (chars[i].size() == 1 && std::isalnum(static_cast<unsigned char>(chars[i][0]))) {
        std::string word = chars[i];
        int32_t j = i + 1;
        while (j < n && chars[j].size() == 1 && std::isalnum(static_cast<unsigned char>(chars[j][0]))) {
          word += chars[j];
          ++j;
        }
        words.push_back(word);
        i = j;
        continue;
      }

      // 尝试匹配词典中的词
      for (int32_t len = std::min(max_len, n - i); len > 1; --len) {
        std::string word;
        for (int32_t j = 0; j < len; ++j) {
          word += chars[i + j];
        }
        if (all_words_.count(word)) {
          words.push_back(word);
          i += len;
          matched = true;
          break;
        }
      }
      
      if (!matched) {
        words.push_back(chars[i]);
        i += 1;
      }
    }
    return words;
  }

  // 应用 FST 规则到当前中文片段
  std::string ApplyImpl(const std::vector<std::string> &words,
                        const std::vector<std::string> &pronunciations) const {
    if (!replacer_list_.empty()) {
      std::vector<std::string> bounded_pronunciations;
      bounded_pronunciations.reserve(pronunciations.size());
      for (const auto &pronunciation : pronunciations) {
        // 规则必须匹配完整音节，避免 hou 从 zhou 内部命中。
        bounded_pronunciations.push_back("#" + pronunciation + "#");
      }
      for (const auto &r : replacer_list_) {
        // 目前仅支持一个规则文件
        return r->Normalize(words, bounded_pronunciations);
      }
    }
    // 没有 FST 规则时，直接拼接原词
    std::string ans;
    for (const auto &w : words) ans.append(w);
    return ans;
  }
  std::string ConvertWordToPronunciation(const std::string &word) const {
    if (word2pron_.count(word)) {
      return word2pron_.at(word);
    }

    if (word.size() <= 3) {
      return word;
    }

    std::vector<std::string> chars = SplitUtf8(word);
    std::string ans;
    for (const auto &c : chars) {
      if (word2pron_.count(c)) {
        ans.append(word2pron_.at(c));
      } else {
        ans.append(c);
      }
    }

    return ans;
  }

  void InitLexicon(std::istream &is) {
    std::string word;
    std::string pron;
    std::string p;
    std::string line;
    int32_t line_num = 0;
    int32_t num_warn = 0;
    
    while (std::getline(is, line)) {
      ++line_num;
      std::istringstream iss(line);

      pron.clear();
      iss >> word;
      ToLowerCase(&word);

      if (word2pron_.count(word)) {
        num_warn += 1;
        if (num_warn < 10) {
          std::cerr << "Warning: Duplicated word: " << word 
                    << " at line " << line_num << ":" << line 
                    << ". Ignore it." << std::endl;
        }
        continue;
      }

      while (iss >> p) {
        if (p.back() > '4') {
          p.push_back('1');
        }
        pron.append(std::move(p));
      }

      if (pron.empty()) {
        std::cerr << "Warning: Empty pronunciation for word '" << word 
                  << "' at line " << line_num << ":" << line 
                  << ". Ignore it." << std::endl;
        continue;
      }

      word2pron_.insert({word, std::move(pron)});
      all_words_.insert(std::move(word));
    }
  }

  // 解析 config_ 中的 add_rules/del_rules/rules_file，构建运行时覆盖词典
  void BuildRuntimeRuleMap() {
    // del set
    std::set<std::string> del_keys(config_.del_rules.begin(), config_.del_rules.end());

    auto add_one = [&](const std::string &line) {
      auto pos = line.find('=');
      if (pos == std::string::npos) return;
      std::string key = line.substr(0, pos);
      std::string val = line.substr(pos + 1);
      if (key.empty() || val.empty()) return;
      if (del_keys.count(key)) return;  // 被删除的键不再添加
      runtime_rule_map_[key] = val;     // 覆盖
    };

    for (const auto &kv : config_.add_rules) add_one(kv);

    if (!config_.rules_file.empty()) {
      std::ifstream fin(config_.rules_file);
      if (fin) {
        std::string line;
        while (std::getline(fin, line)) {
          if (!line.empty()) add_one(line);
        }
      }
    }

    runtime_rule_matcher_.Reset(runtime_rule_map_);
  }

  // 在最终文本上按精确优先、无调唯一目标兜底应用 runtime 规则
  std::string ApplyRuntimeOverrides(const std::string &text) const {
    if (runtime_rule_map_.empty()) return text;

    std::vector<std::string> words = Segment(SplitUtf8(text));
    std::vector<std::string> prons;
    prons.reserve(words.size());
    for (const auto &word : words) {
      if (word.size() < 3 ||
          reinterpret_cast<const uint8_t *>(word.data())[0] < 128) {
        prons.push_back(word);
      } else {
        prons.push_back(ConvertWordToPronunciation(word));
      }
    }

    std::string out;
    for (size_t index = 0; index < words.size();) {
      auto match = runtime_rule_matcher_.FindBest(prons, index);
      if (match) {
        out += match->replacement;
        index = match->end_index;
      } else {
        out += words[index];
        ++index;
      }
    }
    return out;
  }

 private:
  HomophoneReplacerConfig config_;
  std::vector<std::unique_ptr<kaldifst::TextNormalizer>> replacer_list_;
  std::unordered_map<std::string, std::string> word2pron_;
  std::unordered_set<std::string> all_words_;
  std::unordered_map<std::string, std::string> runtime_rule_map_;
  RuntimeRuleMatcher runtime_rule_matcher_;
};

HomophoneReplacer::HomophoneReplacer(const HomophoneReplacerConfig &config)
    : impl_(std::make_unique<Impl>(config)) {}

HomophoneReplacer::~HomophoneReplacer() = default;

std::string HomophoneReplacer::Apply(const std::string &text) const {
  return impl_->Apply(text);
}

}  // namespace hr_standalone
