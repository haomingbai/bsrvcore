#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/bsrvrun/http_request_aspect_handler_factory.h"
#include "bsrvcore/bsrvrun/parameter_map.h"
#include "bsrvcore/bsrvrun/plugin_export.h"
#include "bsrvcore/bsrvrun/string.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/route/http_request_aspect_handler.h"

namespace {

class TestAspect : public bsrvcore::HttpRequestAspectHandler {
 public:
  TestAspect(std::string pre, std::string post, bool append_thread_id)
      : pre_(std::move(pre)),
        post_(std::move(post)),
        append_thread_id_(append_thread_id) {}

  void PreService(
      const std::shared_ptr<bsrvcore::HttpPreServerTask>& task) override {
    task->AppendBody(pre_);
    if (append_thread_id_) {
      std::ostringstream oss;
      oss << std::this_thread::get_id();
      task->AppendBody(oss.str());
    }
  }

  void PostService(
      const std::shared_ptr<bsrvcore::HttpPostServerTask>& task) override {
    task->AppendBody(post_);
    if (append_thread_id_) {
      std::ostringstream oss;
      oss << std::this_thread::get_id();
      task->AppendBody(oss.str());
    }
  }

 private:
  std::string pre_;
  std::string post_;
  bool append_thread_id_;
};

class TestAspectFactory
    : public bsrvcore::bsrvrun::HttpRequestAspectHandlerFactory {
 public:
  bsrvcore::OwnedPtr<bsrvcore::HttpRequestAspectHandler> Get(
      bsrvcore::bsrvrun::ParameterMap* parameters) override {
    std::string pre = "pre|";
    std::string post = "post|";
    bool append_thread_id = false;

    if (parameters != nullptr) {
      const auto pre_value =
          parameters->Get(bsrvcore::bsrvrun::String("pre")).ToStdString();
      const auto post_value =
          parameters->Get(bsrvcore::bsrvrun::String("post")).ToStdString();
      const auto thread_id =
          parameters->Get(bsrvcore::bsrvrun::String("thread_id")).ToStdString();
      if (!pre_value.empty()) {
        pre = pre_value;
      }
      if (!post_value.empty()) {
        post = post_value;
      }
      append_thread_id =
          (thread_id == "1" || thread_id == "true" || thread_id == "TRUE");
    }

    return bsrvcore::AdoptUniqueAs<bsrvcore::HttpRequestAspectHandler>(
        std::make_unique<TestAspect>(pre, post, append_thread_id));
  }
};

TestAspectFactory g_factory;

}  // namespace

BSRVCORE_BSRVRUN_ASPECT_FACTORY_EXPORT GetAspectFactory() { return &g_factory; }
