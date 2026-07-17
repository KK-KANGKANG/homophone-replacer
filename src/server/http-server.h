#ifndef HR_STANDALONE_SERVER_HTTP_SERVER_H_
#define HR_STANDALONE_SERVER_HTTP_SERVER_H_

#include <memory>

#include "server/service-api.h"
#include "server/service-config.h"

namespace hr_standalone {

class HttpServer {
 public:
  HttpServer(ServiceApi &api, const ServiceConfig &config);
  ~HttpServer();

  bool Listen();
  void Stop();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace hr_standalone

#endif  // HR_STANDALONE_SERVER_HTTP_SERVER_H_
