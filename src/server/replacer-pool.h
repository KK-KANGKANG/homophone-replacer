#ifndef HR_STANDALONE_SERVER_REPLACER_POOL_H_
#define HR_STANDALONE_SERVER_REPLACER_POOL_H_

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "server/text-replacer.h"

namespace hr_standalone {

class ReplacerPool {
 public:
  ReplacerPool(size_t worker_count, size_t queue_capacity,
               const TextReplacerFactory &factory);
  ~ReplacerPool();

  ReplacerPool(const ReplacerPool &) = delete;
  ReplacerPool &operator=(const ReplacerPool &) = delete;

  std::optional<std::future<std::string>> Submit(std::string text);
  void Stop(std::chrono::milliseconds timeout);

 private:
  struct Job {
    std::string text;
    std::promise<std::string> promise;
  };

  void Work(std::unique_ptr<TextReplacer> replacer);
  void FailQueuedJobs(const std::string &message);

  size_t queue_capacity_;
  std::mutex mutex_;
  std::condition_variable changed_;
  std::deque<Job> jobs_;
  std::vector<std::thread> workers_;
  bool accepting_ = true;
  bool stopping_ = false;
};

}  // namespace hr_standalone

#endif  // HR_STANDALONE_SERVER_REPLACER_POOL_H_
