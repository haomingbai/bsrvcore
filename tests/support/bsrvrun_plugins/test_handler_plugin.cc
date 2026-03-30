#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "bsrvcore/bsrvrun/http_request_handler_factory.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/route/http_request_handler.h"

namespace {

class TestHandler : public bsrvcore::HttpRequestHandler {
 public:
  TestHandler(std::string body, bool append_thread_id)
      : body_(std::move(body)), append_thread_id_(append_thread_id) {}

  void Service(std::shared_ptr<bsrvcore::HttpServerTask> task) override {
    task->AppendBody(body_);
    if (append_thread_id_) {
      std::ostringstream oss;
      oss << std::this_thread::get_id();
      task->AppendBody(oss.str());
    }
  }

 private:
  std::string body_;
  bool append_thread_id_;
};

class TestHandlerFactory : public bsrvcore::bsrvrun::HttpRequestHandlerFactory {
 public:
  bsrvcore::OwnedPtr<bsrvcore::HttpRequestHandler> Ger(
      bsrvcore::bsrvrun::ParameterMap* parameters) override {
    std::string body = "handler|";
    bool append_thread_id = false;
    if (parameters != nullptr) {
      const auto value =
          parameters->Get(bsrvcore::bsrvrun::String("body")).ToStdString();
      if (!value.empty()) {
        body = value;
      }
      const auto thread_id =
          parameters->Get(bsrvcore::bsrvrun::String("thread_id")).ToStdString();
      append_thread_id =
          (thread_id == "1" || thread_id == "true" || thread_id == "TRUE");
    }

    return bsrvcore::AllocateUnique<TestHandler>(body, append_thread_id);
  }
};

TestHandlerFactory g_factory;

}  // namespace

extern "C" bsrvcore::bsrvrun::HttpRequestHandlerFactory* GetHandlerFactory() {
  return &g_factory;
}
