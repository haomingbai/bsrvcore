#include "internal/callback_adapters.h"

namespace cbind = bsrvcore::c_binding_internal;

extern "C" {

bsrvcore_result_t bsrvcore_server_create(size_t worker_threads,
                                         bsrvcore_server_t** out_server) {
  return cbind::Guard([&]() {
    if (out_server == nullptr) {
      return BSRVCORE_RESULT_INVALID_ARGUMENT;
    }

    *out_server = nullptr;
    auto wrapper = bsrvcore::AllocateUnique<bsrvcore_server>();
    if (worker_threads == 0) {
      wrapper->native = bsrvcore::AllocateUnique<bsrvcore::HttpServer>();
    } else {
      wrapper->native =
          bsrvcore::AllocateUnique<bsrvcore::HttpServer>(worker_threads);
    }
    *out_server = wrapper.release();
    return BSRVCORE_RESULT_OK;
  });
}

void bsrvcore_server_destroy(bsrvcore_server_t* server) {
  bsrvcore::DestroyDeallocate(server);
}

bsrvcore_result_t bsrvcore_server_start(bsrvcore_server_t* server) {
  return cbind::Guard([&]() {
    if (cbind::ValidateServer(server) != BSRVCORE_RESULT_OK) {
      return BSRVCORE_RESULT_INVALID_ARGUMENT;
    }
    if (server->native->IsRunning()) {
      return BSRVCORE_RESULT_ALREADY_RUNNING;
    }
    return server->native->Start() ? BSRVCORE_RESULT_OK
                                   : BSRVCORE_RESULT_OPERATION_FAILED;
  });
}

bsrvcore_result_t bsrvcore_server_stop(bsrvcore_server_t* server) {
  return cbind::Guard([&]() {
    if (cbind::ValidateServer(server) != BSRVCORE_RESULT_OK) {
      return BSRVCORE_RESULT_INVALID_ARGUMENT;
    }
    server->native->Stop();
    return BSRVCORE_RESULT_OK;
  });
}

bsrvcore_result_t bsrvcore_server_is_running(const bsrvcore_server_t* server,
                                             int* out_running) {
  return cbind::Guard([&]() {
    if (cbind::ValidateServer(server) != BSRVCORE_RESULT_OK ||
        out_running == nullptr) {
      return BSRVCORE_RESULT_INVALID_ARGUMENT;
    }
    *out_running = server->native->IsRunning() ? 1 : 0;
    return BSRVCORE_RESULT_OK;
  });
}

bsrvcore_result_t bsrvcore_server_add_listen(bsrvcore_server_t* server,
                                             const char* host, uint16_t port,
                                             size_t io_threads) {
  return cbind::Guard([&]() {
    if (cbind::ValidateServer(server) != BSRVCORE_RESULT_OK ||
        cbind::ValidateStringArg(host) != BSRVCORE_RESULT_OK ||
        io_threads == 0) {
      return BSRVCORE_RESULT_INVALID_ARGUMENT;
    }

    boost::system::error_code ec;
    const auto address = boost::asio::ip::make_address(host, ec);
    if (ec) {
      return BSRVCORE_RESULT_PARSE_ERROR;
    }

    server->native->AddListen({address, port}, io_threads);
    return BSRVCORE_RESULT_OK;
  });
}

bsrvcore_result_t bsrvcore_server_set_header_read_expiry_ms(
    bsrvcore_server_t* server, size_t timeout_ms) {
  return cbind::Guard([&]() {
    if (cbind::ValidateServer(server) != BSRVCORE_RESULT_OK) {
      return BSRVCORE_RESULT_INVALID_ARGUMENT;
    }
    server->native->SetHeaderReadExpiry(timeout_ms);
    return BSRVCORE_RESULT_OK;
  });
}

bsrvcore_result_t bsrvcore_server_set_default_read_expiry_ms(
    bsrvcore_server_t* server, size_t timeout_ms) {
  return cbind::Guard([&]() {
    if (cbind::ValidateServer(server) != BSRVCORE_RESULT_OK) {
      return BSRVCORE_RESULT_INVALID_ARGUMENT;
    }
    server->native->SetDefaultReadExpiry(timeout_ms);
    return BSRVCORE_RESULT_OK;
  });
}

bsrvcore_result_t bsrvcore_server_set_default_write_expiry_ms(
    bsrvcore_server_t* server, size_t timeout_ms) {
  return cbind::Guard([&]() {
    if (cbind::ValidateServer(server) != BSRVCORE_RESULT_OK) {
      return BSRVCORE_RESULT_INVALID_ARGUMENT;
    }
    server->native->SetDefaultWriteExpiry(timeout_ms);
    return BSRVCORE_RESULT_OK;
  });
}

bsrvcore_result_t bsrvcore_server_set_default_max_body_size(
    bsrvcore_server_t* server, size_t max_body_size) {
  return cbind::Guard([&]() {
    if (cbind::ValidateServer(server) != BSRVCORE_RESULT_OK) {
      return BSRVCORE_RESULT_INVALID_ARGUMENT;
    }
    server->native->SetDefaultMaxBodySize(max_body_size);
    return BSRVCORE_RESULT_OK;
  });
}

bsrvcore_result_t bsrvcore_server_set_keep_alive_timeout_ms(
    bsrvcore_server_t* server, size_t timeout_ms) {
  return cbind::Guard([&]() {
    if (cbind::ValidateServer(server) != BSRVCORE_RESULT_OK) {
      return BSRVCORE_RESULT_INVALID_ARGUMENT;
    }
    server->native->SetKeepAliveTimeout(timeout_ms);
    return BSRVCORE_RESULT_OK;
  });
}

bsrvcore_result_t bsrvcore_server_set_route_read_expiry_ms(
    bsrvcore_server_t* server, bsrvcore_http_method_t method, const char* route,
    size_t timeout_ms) {
  return cbind::Guard([&]() {
    bsrvcore::HttpRequestMethod native_method{};
    if (cbind::ValidateServer(server) != BSRVCORE_RESULT_OK ||
        cbind::ValidateStringArg(route) != BSRVCORE_RESULT_OK ||
        !cbind::TryConvertMethod(method, &native_method)) {
      return BSRVCORE_RESULT_INVALID_ARGUMENT;
    }
    server->native->SetReadExpiry(native_method, route, timeout_ms);
    return BSRVCORE_RESULT_OK;
  });
}

bsrvcore_result_t bsrvcore_server_set_route_write_expiry_ms(
    bsrvcore_server_t* server, bsrvcore_http_method_t method, const char* route,
    size_t timeout_ms) {
  return cbind::Guard([&]() {
    bsrvcore::HttpRequestMethod native_method{};
    if (cbind::ValidateServer(server) != BSRVCORE_RESULT_OK ||
        cbind::ValidateStringArg(route) != BSRVCORE_RESULT_OK ||
        !cbind::TryConvertMethod(method, &native_method)) {
      return BSRVCORE_RESULT_INVALID_ARGUMENT;
    }
    server->native->SetWriteExpiry(native_method, route, timeout_ms);
    return BSRVCORE_RESULT_OK;
  });
}

bsrvcore_result_t bsrvcore_server_set_route_max_body_size(
    bsrvcore_server_t* server, bsrvcore_http_method_t method, const char* route,
    size_t max_body_size) {
  return cbind::Guard([&]() {
    bsrvcore::HttpRequestMethod native_method{};
    if (cbind::ValidateServer(server) != BSRVCORE_RESULT_OK ||
        cbind::ValidateStringArg(route) != BSRVCORE_RESULT_OK ||
        !cbind::TryConvertMethod(method, &native_method)) {
      return BSRVCORE_RESULT_INVALID_ARGUMENT;
    }
    server->native->SetMaxBodySize(native_method, route, max_body_size);
    return BSRVCORE_RESULT_OK;
  });
}

bsrvcore_result_t bsrvcore_server_add_route(bsrvcore_server_t* server,
                                            bsrvcore_http_method_t method,
                                            const char* route,
                                            bsrvcore_http_handler_fn handler) {
  return cbind::Guard([&]() {
    bsrvcore::HttpRequestMethod native_method{};
    if (cbind::ValidateServer(server) != BSRVCORE_RESULT_OK ||
        cbind::ValidateStringArg(route) != BSRVCORE_RESULT_OK ||
        handler == nullptr ||
        !cbind::TryConvertMethod(method, &native_method)) {
      return BSRVCORE_RESULT_INVALID_ARGUMENT;
    }
    server->native->AddRouteEntry(
        native_method, route,
        bsrvcore::AllocateUnique<cbind::RouteHandlerAdapter>(handler));
    return BSRVCORE_RESULT_OK;
  });
}

bsrvcore_result_t bsrvcore_server_add_route_with_ctx(
    bsrvcore_server_t* server, bsrvcore_http_method_t method, const char* route,
    bsrvcore_http_handler_ctx_fn handler, void* ctx) {
  return cbind::Guard([&]() {
    bsrvcore::HttpRequestMethod native_method{};
    if (cbind::ValidateServer(server) != BSRVCORE_RESULT_OK ||
        cbind::ValidateStringArg(route) != BSRVCORE_RESULT_OK ||
        handler == nullptr ||
        !cbind::TryConvertMethod(method, &native_method)) {
      return BSRVCORE_RESULT_INVALID_ARGUMENT;
    }
    server->native->AddRouteEntry(
        native_method, route,
        bsrvcore::AllocateUnique<cbind::RouteHandlerAdapter>(handler, ctx));
    return BSRVCORE_RESULT_OK;
  });
}

bsrvcore_result_t bsrvcore_server_add_global_aspect(
    bsrvcore_server_t* server, bsrvcore_http_pre_aspect_fn pre_aspect,
    bsrvcore_http_post_aspect_fn post_aspect) {
  return cbind::Guard([&]() {
    if (cbind::ValidateServer(server) != BSRVCORE_RESULT_OK ||
        (pre_aspect == nullptr && post_aspect == nullptr)) {
      return BSRVCORE_RESULT_INVALID_ARGUMENT;
    }
    server->native->AddGlobalAspect(
        bsrvcore::AllocateUnique<cbind::AspectHandlerAdapter>(pre_aspect,
                                                              post_aspect));
    return BSRVCORE_RESULT_OK;
  });
}

bsrvcore_result_t bsrvcore_server_add_global_aspect_with_ctx(
    bsrvcore_server_t* server, bsrvcore_http_pre_aspect_ctx_fn pre_aspect,
    bsrvcore_http_post_aspect_ctx_fn post_aspect, void* ctx) {
  return cbind::Guard([&]() {
    if (cbind::ValidateServer(server) != BSRVCORE_RESULT_OK ||
        (pre_aspect == nullptr && post_aspect == nullptr)) {
      return BSRVCORE_RESULT_INVALID_ARGUMENT;
    }
    server->native->AddGlobalAspect(
        bsrvcore::AllocateUnique<cbind::AspectHandlerAdapter>(
            pre_aspect, post_aspect, ctx));
    return BSRVCORE_RESULT_OK;
  });
}

bsrvcore_result_t bsrvcore_server_add_method_global_aspect(
    bsrvcore_server_t* server, bsrvcore_http_method_t method,
    bsrvcore_http_pre_aspect_fn pre_aspect,
    bsrvcore_http_post_aspect_fn post_aspect) {
  return cbind::Guard([&]() {
    bsrvcore::HttpRequestMethod native_method{};
    if (cbind::ValidateServer(server) != BSRVCORE_RESULT_OK ||
        (pre_aspect == nullptr && post_aspect == nullptr) ||
        !cbind::TryConvertMethod(method, &native_method)) {
      return BSRVCORE_RESULT_INVALID_ARGUMENT;
    }
    server->native->AddGlobalAspect(
        native_method, bsrvcore::AllocateUnique<cbind::AspectHandlerAdapter>(
                           pre_aspect, post_aspect));
    return BSRVCORE_RESULT_OK;
  });
}

bsrvcore_result_t bsrvcore_server_add_method_global_aspect_with_ctx(
    bsrvcore_server_t* server, bsrvcore_http_method_t method,
    bsrvcore_http_pre_aspect_ctx_fn pre_aspect,
    bsrvcore_http_post_aspect_ctx_fn post_aspect, void* ctx) {
  return cbind::Guard([&]() {
    bsrvcore::HttpRequestMethod native_method{};
    if (cbind::ValidateServer(server) != BSRVCORE_RESULT_OK ||
        (pre_aspect == nullptr && post_aspect == nullptr) ||
        !cbind::TryConvertMethod(method, &native_method)) {
      return BSRVCORE_RESULT_INVALID_ARGUMENT;
    }
    server->native->AddGlobalAspect(
        native_method, bsrvcore::AllocateUnique<cbind::AspectHandlerAdapter>(
                           pre_aspect, post_aspect, ctx));
    return BSRVCORE_RESULT_OK;
  });
}

bsrvcore_result_t bsrvcore_server_add_route_aspect(
    bsrvcore_server_t* server, bsrvcore_http_method_t method, const char* route,
    bsrvcore_http_pre_aspect_fn pre_aspect,
    bsrvcore_http_post_aspect_fn post_aspect) {
  return cbind::Guard([&]() {
    bsrvcore::HttpRequestMethod native_method{};
    if (cbind::ValidateServer(server) != BSRVCORE_RESULT_OK ||
        cbind::ValidateStringArg(route) != BSRVCORE_RESULT_OK ||
        (pre_aspect == nullptr && post_aspect == nullptr) ||
        !cbind::TryConvertMethod(method, &native_method)) {
      return BSRVCORE_RESULT_INVALID_ARGUMENT;
    }
    server->native->AddAspect(
        native_method, route,
        bsrvcore::AllocateUnique<cbind::AspectHandlerAdapter>(pre_aspect,
                                                              post_aspect));
    return BSRVCORE_RESULT_OK;
  });
}

bsrvcore_result_t bsrvcore_server_add_route_aspect_with_ctx(
    bsrvcore_server_t* server, bsrvcore_http_method_t method, const char* route,
    bsrvcore_http_pre_aspect_ctx_fn pre_aspect,
    bsrvcore_http_post_aspect_ctx_fn post_aspect, void* ctx) {
  return cbind::Guard([&]() {
    bsrvcore::HttpRequestMethod native_method{};
    if (cbind::ValidateServer(server) != BSRVCORE_RESULT_OK ||
        cbind::ValidateStringArg(route) != BSRVCORE_RESULT_OK ||
        (pre_aspect == nullptr && post_aspect == nullptr) ||
        !cbind::TryConvertMethod(method, &native_method)) {
      return BSRVCORE_RESULT_INVALID_ARGUMENT;
    }
    server->native->AddAspect(
        native_method, route,
        bsrvcore::AllocateUnique<cbind::AspectHandlerAdapter>(
            pre_aspect, post_aspect, ctx));
    return BSRVCORE_RESULT_OK;
  });
}

bsrvcore_result_t bsrvcore_server_add_terminal_aspect(
    bsrvcore_server_t* server, bsrvcore_http_method_t method, const char* route,
    bsrvcore_http_pre_aspect_fn pre_aspect,
    bsrvcore_http_post_aspect_fn post_aspect) {
  return cbind::Guard([&]() {
    bsrvcore::HttpRequestMethod native_method{};
    if (cbind::ValidateServer(server) != BSRVCORE_RESULT_OK ||
        cbind::ValidateStringArg(route) != BSRVCORE_RESULT_OK ||
        (pre_aspect == nullptr && post_aspect == nullptr) ||
        !cbind::TryConvertMethod(method, &native_method)) {
      return BSRVCORE_RESULT_INVALID_ARGUMENT;
    }
    server->native->AddTerminalAspect(
        native_method, route,
        bsrvcore::AllocateUnique<cbind::AspectHandlerAdapter>(pre_aspect,
                                                              post_aspect));
    return BSRVCORE_RESULT_OK;
  });
}

bsrvcore_result_t bsrvcore_server_add_terminal_aspect_with_ctx(
    bsrvcore_server_t* server, bsrvcore_http_method_t method, const char* route,
    bsrvcore_http_pre_aspect_ctx_fn pre_aspect,
    bsrvcore_http_post_aspect_ctx_fn post_aspect, void* ctx) {
  return cbind::Guard([&]() {
    bsrvcore::HttpRequestMethod native_method{};
    if (cbind::ValidateServer(server) != BSRVCORE_RESULT_OK ||
        cbind::ValidateStringArg(route) != BSRVCORE_RESULT_OK ||
        (pre_aspect == nullptr && post_aspect == nullptr) ||
        !cbind::TryConvertMethod(method, &native_method)) {
      return BSRVCORE_RESULT_INVALID_ARGUMENT;
    }
    server->native->AddTerminalAspect(
        native_method, route,
        bsrvcore::AllocateUnique<cbind::AspectHandlerAdapter>(
            pre_aspect, post_aspect, ctx));
    return BSRVCORE_RESULT_OK;
  });
}

}  // extern "C"
