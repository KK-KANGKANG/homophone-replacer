#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "runtime-rule-matcher.h"

using hr_standalone::RuntimeRuleMatcher;

#define CHECK(condition)                                                \
  do {                                                                  \
    if (!(condition)) {                                                 \
      std::cerr << "Check failed at line " << __LINE__ << ": "       \
                << #condition << std::endl;                             \
      return 1;                                                         \
    }                                                                   \
  } while (false)

int main() {
  RuntimeRuleMatcher matcher;
  matcher.Reset({{"luan3chao2", "卵巢"}});

  auto exact = matcher.FindBest({"luan3", "chao2"}, 0);
  CHECK(exact && exact->replacement == "卵巢" && exact->exact);

  auto flexible = matcher.FindBest({"luan4", "chao2"}, 0);
  CHECK(flexible && flexible->replacement == "卵巢" && !flexible->exact);

  matcher.Reset({
      {"fei4", "肺"},
      {"fei4jing4mai4", "肺静脉"},
      {"fei2jing4mai4", "腓静脉"},
  });
  CHECK(matcher.FindBest({"fei4", "jing4", "mai4"}, 0)->replacement ==
        "肺静脉");
  CHECK(matcher.FindBest({"fei2", "jing4", "mai4"}, 0)->replacement ==
        "腓静脉");
  CHECK(!matcher.FindBest({"fei3", "jing4", "mai4"}, 0));
  return 0;
}
