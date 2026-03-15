#include <memory>
#include <string>

#include "bsrvcore/bsrvrun/http_request_aspect_handler_factory.h"
#include "bsrvcore/http_request_aspect_handler.h"
#include "bsrvcore/http_server_task.h"

namespace {

class TestAspect : public bsrvcore::HttpRequestAspectHandler {
 public:
  TestAspect(std::string pre, std::string post)
      : pre_(std::move(pre)), post_(std::move(post)) {}

  void PreService(std::shared_ptr<bsrvcore::HttpPreServerTask> task) override {
    task->AppendBody(pre_);
  }

  void PostService(std::shared_ptr<bsrvcore::HttpPostServerTask> task) override {
    task->AppendBody(post_);
  }

 private:
  std::string pre_;
  std::string post_;
};

class TestAspectFactory
    : public bsrvcore::bsrvrun::HttpRequestAspectHandlerFactory {
 public:
  std::unique_ptr<bsrvcore::HttpRequestAspectHandler> Ger(
      bsrvcore::bsrvrun::ParameterMap* parameters) override {
    std::string pre = "pre|";
    std::string post = "post|";

    if (parameters != nullptr) {
      const auto pre_value =
          parameters->Get(bsrvcore::bsrvrun::String("pre")).ToStdString();
      const auto post_value =
          parameters->Get(bsrvcore::bsrvrun::String("post")).ToStdString();
      if (!pre_value.empty()) {
        pre = pre_value;
      }
      if (!post_value.empty()) {
        post = post_value;
      }
    }

    return std::make_unique<TestAspect>(pre, post);
  }
};

TestAspectFactory g_factory;

}  // namespace

extern "C" bsrvcore::bsrvrun::HttpRequestAspectHandlerFactory* GetAspectFactory() {
  return &g_factory;
}
