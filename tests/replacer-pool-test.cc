#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

#include "server/replacer-pool.h"

using hr_standalone::ReplacerPool;
using hr_standalone::TextReplacer;

#define CHECK(condition)                                                \
  do {                                                                  \
    if (!(condition)) {                                                 \
      std::cerr << "Check failed at line " << __LINE__ << ": "       \
                << #condition << std::endl;                             \
      return 1;                                                         \
    }                                                                   \
  } while (false)

namespace {

class FakeReplacer final : public TextReplacer {
 public:
  explicit FakeReplacer(int id) : id_(id) {}

  std::string Replace(const std::string &text) override {
    if (text == "throw") throw std::runtime_error("fake failure");
    return std::to_string(id_) + ":" + text;
  }

 private:
  int id_;
};

struct Gate {
  std::mutex mutex;
  std::condition_variable changed;
  bool entered = false;
  bool released = false;
};

class BlockingReplacer final : public TextReplacer {
 public:
  explicit BlockingReplacer(std::shared_ptr<Gate> gate)
      : gate_(std::move(gate)) {}

  std::string Replace(const std::string &text) override {
    std::unique_lock<std::mutex> lock(gate_->mutex);
    gate_->entered = true;
    gate_->changed.notify_all();
    gate_->changed.wait(lock, [&] { return gate_->released; });
    return text;
  }

 private:
  std::shared_ptr<Gate> gate_;
};

}  // namespace

int main() {
  std::atomic<int> factories{0};
  ReplacerPool pool(2, 4, [&] {
    return std::make_unique<FakeReplacer>(factories.fetch_add(1));
  });
  auto first = pool.Submit("one");
  auto second = pool.Submit("two");
  CHECK(first.has_value());
  CHECK(second.has_value());
  CHECK(first->get().find(":one") != std::string::npos);
  CHECK(second->get().find(":two") != std::string::npos);
  CHECK(factories.load() == 2);

  auto failed = pool.Submit("throw");
  CHECK(failed.has_value());
  bool exception_seen = false;
  try {
    failed->get();
  } catch (const std::runtime_error &) {
    exception_seen = true;
  }
  CHECK(exception_seen);
  auto after_failure = pool.Submit("alive");
  CHECK(after_failure.has_value());
  CHECK(after_failure->get().find(":alive") != std::string::npos);
  pool.Stop(std::chrono::seconds(1));
  CHECK(!pool.Submit("stopped").has_value());

  auto gate = std::make_shared<Gate>();
  ReplacerPool bounded(1, 1, [gate] {
    return std::make_unique<BlockingReplacer>(gate);
  });
  auto running = bounded.Submit("running");
  {
    std::unique_lock<std::mutex> lock(gate->mutex);
    gate->changed.wait(lock, [&] { return gate->entered; });
  }
  auto queued = bounded.Submit("queued");
  CHECK(queued.has_value());
  CHECK(!bounded.Submit("rejected").has_value());
  {
    std::lock_guard<std::mutex> lock(gate->mutex);
    gate->released = true;
  }
  gate->changed.notify_all();
  CHECK(running->get() == "running");
  CHECK(queued->get() == "queued");
  bounded.Stop(std::chrono::seconds(1));
  return 0;
}
