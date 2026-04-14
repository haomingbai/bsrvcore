#include "internal/callback_adapters.h"

namespace bsrvcore::c_binding_internal {

RouteHandlerAdapter::RouteHandlerAdapter(bsrvcore_http_handler_fn fn)
    : fn_(fn) {}

RouteHandlerAdapter::RouteHandlerAdapter(bsrvcore_http_handler_ctx_fn fn,
                                         void* ctx)
    : ctx_fn_(fn), ctx_(ctx) {}

void RouteHandlerAdapter::Service(
    std::shared_ptr<bsrvcore::HttpServerTask> task) {
  bsrvcore_http_server_task_t wrapper{task.get()};
  try {
    if (ctx_fn_ != nullptr) {
      ctx_fn_(&wrapper, ctx_);
      return;
    }
    if (fn_ != nullptr) {
      fn_(&wrapper);
    }
  } catch (...) {
    SetInternalErrorResponse(task.get());
  }
}

AspectHandlerAdapter::AspectHandlerAdapter(bsrvcore_http_pre_aspect_fn pre_fn,
                                           bsrvcore_http_post_aspect_fn post_fn)
    : pre_fn_(pre_fn), post_fn_(post_fn) {}

AspectHandlerAdapter::AspectHandlerAdapter(
    bsrvcore_http_pre_aspect_ctx_fn pre_fn,
    bsrvcore_http_post_aspect_ctx_fn post_fn, void* ctx)
    : pre_ctx_fn_(pre_fn), post_ctx_fn_(post_fn), ctx_(ctx) {}

void AspectHandlerAdapter::PreService(
    std::shared_ptr<bsrvcore::HttpPreServerTask> task) {
  bsrvcore_http_pre_server_task_t wrapper{task.get()};
  try {
    if (pre_ctx_fn_ != nullptr) {
      pre_ctx_fn_(&wrapper, ctx_);
      return;
    }
    if (pre_fn_ != nullptr) {
      pre_fn_(&wrapper);
    }
  } catch (...) {
    SetInternalErrorResponse(task.get());
  }
}

void AspectHandlerAdapter::PostService(
    std::shared_ptr<bsrvcore::HttpPostServerTask> task) {
  bsrvcore_http_post_server_task_t wrapper{task.get()};
  try {
    if (post_ctx_fn_ != nullptr) {
      post_ctx_fn_(&wrapper, ctx_);
      return;
    }
    if (post_fn_ != nullptr) {
      post_fn_(&wrapper);
    }
  } catch (...) {
    SetInternalErrorResponse(task.get());
  }
}

}  // namespace bsrvcore::c_binding_internal
