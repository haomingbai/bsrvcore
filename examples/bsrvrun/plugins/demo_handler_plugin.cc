/**
 * @file demo_handler_plugin.cc
 * @brief Minimal bsrvrun handler plugin used by examples and tests.
 *
 * The exported factory returns a handler that appends a configured body suffix.
 * This keeps the plugin ABI example intentionally small and easy to copy.
 */

#include <memory>
#include <string>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/bsrvrun/http_request_handler_factory.h"
#include "bsrvcore/bsrvrun/parameter_map.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/route/http_request_handler.h"

namespace {

class DemoHandler : public bsrvcore::HttpRequestHandler {
 public:
  explicit DemoHandler(std::string body) : body_(std::move(body)) {}

  void Service(std::shared_ptr<bsrvcore::HttpServerTask> task) override {
    task->AppendBody(body_);
  }

 private:
  std::string body_;
};

class DemoHandlerFactory : public bsrvcore::bsrvrun::HttpRequestHandlerFactory {
 public:
  bsrvcore::OwnedPtr<bsrvcore::HttpRequestHandler> Ger(
      bsrvcore::bsrvrun::ParameterMap* params) override {
    std::string body = "demo-handler|";
    if (params != nullptr) {
      const auto body_value =
          params->Get(bsrvcore::bsrvrun::String("body")).ToStdString();
      if (!body_value.empty()) {
        body = body_value;
      }
    }
    return bsrvcore::AllocateUnique<DemoHandler>(body);
  }
};

DemoHandlerFactory g_factory;

}  // namespace

BSRVCORE_BSRVRUN_HANDLER_FACTORY_EXPORT GetHandlerFactory() {
  // bsrvrun resolves this exact symbol name at runtime.
  return &g_factory;
}
