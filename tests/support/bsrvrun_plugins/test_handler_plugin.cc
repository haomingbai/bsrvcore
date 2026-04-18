#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#include "bsrvcore/bsrvrun/http_request_handler_factory.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/route/http_request_handler.h"
#include "test_service_api.h"

namespace {

std::optional<std::size_t> ParseOptionalSlot(
    bsrvcore::bsrvrun::ParameterMap* parameters) {
  if (parameters == nullptr) {
    return std::nullopt;
  }

  const auto slot_value =
      parameters->Get(bsrvcore::bsrvrun::String("service_slot")).ToStdString();
  if (slot_value.empty()) {
    return std::nullopt;
  }

  try {
    return static_cast<std::size_t>(std::stoull(slot_value));
  } catch (...) {
    return std::nullopt;
  }
}

class TestHandler : public bsrvcore::HttpRequestHandler {
 public:
  TestHandler(std::string body, bool append_thread_id,
              std::optional<std::size_t> service_slot)
      : body_(std::move(body)),
        append_thread_id_(append_thread_id),
        service_slot_(service_slot) {}

  void Service(const std::shared_ptr<bsrvcore::HttpServerTask>& task) override {
    if (service_slot_.has_value()) {
      if (auto* service = task->GetService<TestServiceData>(*service_slot_)) {
        task->AppendBody(service->body);
      }
    }
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
  std::optional<std::size_t> service_slot_;
};

class TestHandlerFactory : public bsrvcore::bsrvrun::HttpRequestHandlerFactory {
 public:
  bsrvcore::OwnedPtr<bsrvcore::HttpRequestHandler> Get(
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

    return bsrvcore::AllocateUnique<TestHandler>(body, append_thread_id,
                                                 ParseOptionalSlot(parameters));
  }
};

TestHandlerFactory g_factory;

}  // namespace

BSRVCORE_BSRVRUN_HANDLER_FACTORY_EXPORT GetHandlerFactory() {
  return &g_factory;
}
