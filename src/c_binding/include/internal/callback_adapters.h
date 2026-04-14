#pragma once

#ifndef BSRVCORE_C_BINDING_INTERNAL_CALLBACK_ADAPTERS_H_
#define BSRVCORE_C_BINDING_INTERNAL_CALLBACK_ADAPTERS_H_

#include "internal/common.h"

namespace bsrvcore::c_binding_internal {

class RouteHandlerAdapter : public bsrvcore::HttpRequestHandler {
 public:
  explicit RouteHandlerAdapter(bsrvcore_http_handler_fn fn);
  RouteHandlerAdapter(bsrvcore_http_handler_ctx_fn fn, void* ctx);

  void Service(std::shared_ptr<bsrvcore::HttpServerTask> task) override;

 private:
  bsrvcore_http_handler_fn fn_{nullptr};
  bsrvcore_http_handler_ctx_fn ctx_fn_{nullptr};
  void* ctx_{nullptr};
};

class AspectHandlerAdapter : public bsrvcore::HttpRequestAspectHandler {
 public:
  AspectHandlerAdapter(bsrvcore_http_pre_aspect_fn pre_fn,
                       bsrvcore_http_post_aspect_fn post_fn);
  AspectHandlerAdapter(bsrvcore_http_pre_aspect_ctx_fn pre_fn,
                       bsrvcore_http_post_aspect_ctx_fn post_fn, void* ctx);

  void PreService(std::shared_ptr<bsrvcore::HttpPreServerTask> task) override;
  void PostService(std::shared_ptr<bsrvcore::HttpPostServerTask> task) override;

 private:
  bsrvcore_http_pre_aspect_fn pre_fn_{nullptr};
  bsrvcore_http_post_aspect_fn post_fn_{nullptr};
  bsrvcore_http_pre_aspect_ctx_fn pre_ctx_fn_{nullptr};
  bsrvcore_http_post_aspect_ctx_fn post_ctx_fn_{nullptr};
  void* ctx_{nullptr};
};

}  // namespace bsrvcore::c_binding_internal

#endif  // BSRVCORE_C_BINDING_INTERNAL_CALLBACK_ADAPTERS_H_
