#include "server/replacer-pool.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace hr_standalone {

ReplacerPool::ReplacerPool(size_t worker_count, size_t queue_capacity,
                           const TextReplacerFactory &factory)
    : queue_capacity_(queue_capacity) {
  if (worker_count == 0 || queue_capacity == 0) {
    throw std::invalid_argument("worker count and queue capacity must be positive");
  }
  std::vector<std::unique_ptr<TextReplacer>> replacers;
  replacers.reserve(worker_count);
  for (size_t index = 0; index < worker_count; ++index) {
    auto replacer = factory();
    if (!replacer) throw std::runtime_error("replacer factory returned null");
    replacers.push_back(std::move(replacer));
  }
  workers_.reserve(worker_count);
  for (auto &replacer : replacers) {
    workers_.emplace_back(&ReplacerPool::Work, this, std::move(replacer));
  }
}

ReplacerPool::~ReplacerPool() { Stop(std::chrono::seconds(10)); }

std::optional<std::future<std::string>> ReplacerPool::Submit(
    std::string text) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!accepting_ || jobs_.size() >= queue_capacity_) return std::nullopt;
  Job job;
  job.text = std::move(text);
  auto future = job.promise.get_future();
  jobs_.push_back(std::move(job));
  changed_.notify_one();
  return std::optional<std::future<std::string>>(std::move(future));
}

void ReplacerPool::Stop(std::chrono::milliseconds timeout) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) return;
    accepting_ = false;
    stopping_ = true;
  }
  changed_.notify_all();
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (jobs_.empty()) break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!jobs_.empty()) FailQueuedJobs("service is stopping");
  }
  changed_.notify_all();
  for (auto &worker : workers_) {
    if (worker.joinable()) worker.join();
  }
}

void ReplacerPool::Work(std::unique_ptr<TextReplacer> replacer) {
  while (true) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      changed_.wait(lock, [&] { return stopping_ || !jobs_.empty(); });
      if (jobs_.empty()) {
        if (stopping_) return;
        continue;
      }
      job = std::move(jobs_.front());
      jobs_.pop_front();
    }
    try {
      job.promise.set_value(replacer->Replace(job.text));
    } catch (...) {
      job.promise.set_exception(std::current_exception());
    }
  }
}

void ReplacerPool::FailQueuedJobs(const std::string &message) {
  while (!jobs_.empty()) {
    jobs_.front().promise.set_exception(
        std::make_exception_ptr(std::runtime_error(message)));
    jobs_.pop_front();
  }
}

}  // namespace hr_standalone
