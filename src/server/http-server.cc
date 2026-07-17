#include "server/http-server.h"

#include <chrono>
#include <string>

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace hr_standalone {
namespace {

void WriteResponse(const ApiResponse &source, httplib::Response *response) {
  response->status = source.status;
  response->set_content(source.body, "application/json; charset=utf-8");
}

}  // namespace

class HttpServer::Impl {
 public:
  Impl(ServiceApi &api, const ServiceConfig &config)
      : api_(api), host_(config.server.host), port_(config.server.port) {
    server_.set_payload_max_length(config.server.max_text_characters * 4 + 1024);
    server_.Get("/health", [this](const auto &, auto &response) {
      WriteResponse(api_.Health(), &response);
    });
    server_.Post("/replace", [this](const auto &request, auto &response) {
      auto started = std::chrono::steady_clock::now();
      if (request.get_header_value("Content-Type").find("application/json") ==
          std::string::npos) {
        WriteResponse({400, R"({"code":40005,"message":"Content-Type must be application/json"})"},
                      &response);
      } else {
        WriteResponse(api_.Replace(request.body), &response);
      }
      double elapsed = std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - started)
                           .count();
      spdlog::info("method=POST path=/replace status={} bytes={} elapsed_ms={:.3f}",
                   response.status, request.body.size(), elapsed);
    });
    server_.set_error_handler([](const auto &, auto &response) {
      int status = response.status > 0 ? response.status : 404;
      response.status = status;
      response.set_content(
          nlohmann::json{{"code", status == 404 ? 40401 : 50002},
                         {"message", status == 404 ? "not found" : "HTTP error"}}
              .dump(),
          "application/json; charset=utf-8");
    });
  }

  bool Listen() { return server_.listen(host_, port_); }
  void Stop() { server_.stop(); }

 private:
  ServiceApi &api_;
  std::string host_;
  int port_;
  httplib::Server server_;
};

HttpServer::HttpServer(ServiceApi &api, const ServiceConfig &config)
    : impl_(std::make_unique<Impl>(api, config)) {}

HttpServer::~HttpServer() = default;
bool HttpServer::Listen() { return impl_->Listen(); }
void HttpServer::Stop() { impl_->Stop(); }

}  // namespace hr_standalone
