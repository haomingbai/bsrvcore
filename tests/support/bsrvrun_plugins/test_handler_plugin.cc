#include <memory>
#include <string>

#include "bsrvcore/bsrvrun/http_request_handler_factory.h"
#include "bsrvcore/http_request_handler.h"
#include "bsrvcore/http_server_task.h"

namespace {

class TestHandler : public bsrvcore::HttpRequestHandler {
 public:
  explicit TestHandler(std::string body) : body_(std::move(body)) {}

  void Service(std::shared_ptr<bsrvcore::HttpServerTask> task) override {
    task->AppendBody(body_);
  }

 private:
  std::string body_;
};

class TestHandlerFactory : public bsrvcore::bsrvrun::HttpRequestHandlerFactory {
 public:
  std::unique_ptr<bsrvcore::HttpRequestHandler> Ger(
      bsrvcore::bsrvrun::ParameterMap* parameters) override {
    std::string body = "handler|";
    if (parameters != nullptr) {
      const auto value =
          parameters->Get(bsrvcore::bsrvrun::String("body")).ToStdString();
      if (!value.empty()) {
        body = value;
      }
    }

    return std::make_unique<TestHandler>(body);
  }
};

TestHandlerFactory g_factory;

}  // namespace

extern "C" bsrvcore::bsrvrun::HttpRequestHandlerFactory* GetHandlerFactory() {
  return &g_factory;
}
