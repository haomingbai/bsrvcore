#include <memory>
#include <string>

#include "bsrvcore/bsrvrun/http_request_aspect_handler_factory.h"
#include "bsrvcore/http_request_aspect_handler.h"
#include "bsrvcore/http_server_task.h"

namespace {

class DemoAspect : public bsrvcore::HttpRequestAspectHandler {
 public:
  DemoAspect(std::string pre, std::string post)
      : pre_(std::move(pre)), post_(std::move(post)) {}

  void PreService(std::shared_ptr<bsrvcore::HttpPreServerTask> task) override {
    task->AppendBody(pre_);
  }

  void PostService(
      std::shared_ptr<bsrvcore::HttpPostServerTask> task) override {
    task->AppendBody(post_);
  }

 private:
  std::string pre_;
  std::string post_;
};

class DemoAspectFactory
    : public bsrvcore::bsrvrun::HttpRequestAspectHandlerFactory {
 public:
  bsrvcore::OwnedPtr<bsrvcore::HttpRequestAspectHandler> Ger(
      bsrvcore::bsrvrun::ParameterMap* params) override {
    std::string pre = "demo-pre|";
    std::string post = "demo-post|";

    if (params != nullptr) {
      const auto pre_value =
          params->Get(bsrvcore::bsrvrun::String("pre")).ToStdString();
      const auto post_value =
          params->Get(bsrvcore::bsrvrun::String("post")).ToStdString();
      if (!pre_value.empty()) {
        pre = pre_value;
      }
      if (!post_value.empty()) {
        post = post_value;
      }
    }

    return bsrvcore::AllocateUnique<DemoAspect>(pre, post);
  }
};

DemoAspectFactory g_factory;

}  // namespace

extern "C" bsrvcore::bsrvrun::HttpRequestAspectHandlerFactory*
GetAspectFactory() {
  return &g_factory;
}
