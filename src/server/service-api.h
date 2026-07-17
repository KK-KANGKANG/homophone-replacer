#ifndef HR_STANDALONE_SERVER_SERVICE_API_H_
#define HR_STANDALONE_SERVER_SERVICE_API_H_

#include <atomic>
#include <string>
#include <string_view>

#include "server/replacer-pool.h"
#include "server/service-config.h"

namespace hr_standalone {

struct ApiResponse {
  int status;
  std::string body;
};

class ServiceApi {
 public:
  ServiceApi(ReplacerPool &pool, const ServiceConfig &config,
             bool lexicon_loaded, bool fst_loaded, bool mapping_loaded);

  ApiResponse Replace(std::string_view json_body);
  ApiResponse Health() const;
  void SetStopping();

 private:
  ReplacerPool &pool_;
  size_t max_text_characters_;
  bool lexicon_loaded_;
  bool fst_loaded_;
  bool mapping_enabled_;
  bool mapping_loaded_;
  std::atomic<bool> stopping_{false};
};

}  // namespace hr_standalone

#endif  // HR_STANDALONE_SERVER_SERVICE_API_H_
