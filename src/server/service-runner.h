#ifndef HR_STANDALONE_SERVER_SERVICE_RUNNER_H_
#define HR_STANDALONE_SERVER_SERVICE_RUNNER_H_

#include <atomic>

#include "server/service-config.h"

namespace hr_standalone {

class StopController {
 public:
  void RequestStop();
  bool IsStopping() const;

 private:
  std::atomic<bool> stopping_{false};
};

int RunService(const ServiceConfig &config, StopController &stop);
void InstallForegroundStopHandlers(StopController *stop);

}  // namespace hr_standalone

#endif  // HR_STANDALONE_SERVER_SERVICE_RUNNER_H_
