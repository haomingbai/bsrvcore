#include <memory>
#include <string>

#include "bsrvcore/bsrvrun/http_request_handler_factory.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/route/http_request_handler.h"

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
  bsrvcore::OwnedPtr<bsrvcore::HttpRequestHandler> Ger(
      bsrvcore::bsrvrun::ParameterMap* parameters) override {
    std::string body = "handler|";
    if (parameters != nullptr) {
      const auto value =
          parameters->Get(bsrvcore::bsrvrun::String("body")).ToStdString();
      if (!value.empty()) {
        body = value;
      }
    }

    return bsrvcore::AllocateUnique<TestHandler>(body);
  }
};

TestHandlerFactory g_factory;

}  // namespace

extern "C" bsrvcore::bsrvrun::HttpRequestHandlerFactory* GetHandlerFactory() {
  return &g_factory;
}
