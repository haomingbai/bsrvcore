#include <gtest/gtest.h>

#include <boost/beast/http.hpp>
#include <string>

#include "bsrvcore-c/bsrvcore.h"
#include "test_http_client_task.h"

struct RouteContext {
  const char* prefix;
};

struct AspectContext {
  const char* pre_marker;
  const char* post_marker;
};

extern "C" void hello_handler(bsrvcore_http_server_task_t* task) {
  (void)bsrvcore_http_server_task_set_response(
      task, 200, "text/plain; charset=utf-8", "hello", 5);
}

extern "C" void echo_handler(bsrvcore_http_server_task_t* task, void* ctx) {
  auto* route_ctx = static_cast<RouteContext*>(ctx);
  const char* header_value = nullptr;
  size_t header_len = 0;
  const char* body = nullptr;
  size_t body_len = 0;

  if (bsrvcore_http_server_task_get_request_header(
          task, "X-Test", &header_value, &header_len) != BSRVCORE_RESULT_OK ||
      bsrvcore_http_server_task_get_request_body(task, &body, &body_len) !=
          BSRVCORE_RESULT_OK) {
    (void)bsrvcore_http_server_task_set_response(
        task, 500, "text/plain; charset=utf-8", "callback error", 14);
    return;
  }

  (void)bsrvcore_http_server_task_set_status(task, 200);
  (void)bsrvcore_http_server_task_set_response_header(task, "X-Reply",
                                                      route_ctx->prefix);

  std::string response(route_ctx->prefix);
  response.push_back(':');
  response.append(header_value, header_len);
  response.push_back(':');
  response.append(body, body_len);
  (void)bsrvcore_http_server_task_set_response_body(task, response.data(),
                                                    response.size());
}

extern "C" void pre_global(bsrvcore_http_pre_server_task_t* task) {
  (void)bsrvcore_http_pre_server_task_append_response_body(task, "preG|", 5);
}

extern "C" void pre_method(bsrvcore_http_pre_server_task_t* task, void* ctx) {
  auto* aspect_ctx = static_cast<AspectContext*>(ctx);
  (void)bsrvcore_http_pre_server_task_append_response_body(
      task, aspect_ctx->pre_marker,
      std::char_traits<char>::length(aspect_ctx->pre_marker));
}

extern "C" void pre_subtree(bsrvcore_http_pre_server_task_t* task) {
  (void)bsrvcore_http_pre_server_task_append_response_body(task, "preS|", 5);
}

extern "C" void order_handler(bsrvcore_http_server_task_t* task) {
  (void)bsrvcore_http_server_task_append_response_body(task, "handler|", 8);
}

extern "C" void post_subtree(bsrvcore_http_post_server_task_t* task) {
  (void)bsrvcore_http_post_server_task_append_response_body(task, "postS|", 6);
}

extern "C" void pre_terminal(bsrvcore_http_pre_server_task_t* task) {
  (void)bsrvcore_http_pre_server_task_append_response_body(task, "preT|", 5);
}

extern "C" void post_terminal(bsrvcore_http_post_server_task_t* task) {
  (void)bsrvcore_http_post_server_task_append_response_body(task, "postT|", 6);
}

extern "C" void post_method(bsrvcore_http_post_server_task_t* task, void* ctx) {
  auto* aspect_ctx = static_cast<AspectContext*>(ctx);
  (void)bsrvcore_http_post_server_task_append_response_body(
      task, aspect_ctx->post_marker,
      std::char_traits<char>::length(aspect_ctx->post_marker));
}

extern "C" void post_global(bsrvcore_http_post_server_task_t* task) {
  (void)bsrvcore_http_post_server_task_append_response_body(task, "postG|", 6);
}

namespace {

using bsrvcore::test::DoRequestWithRetry;
using bsrvcore::test::FindFreePort;
namespace http = boost::beast::http;

struct CServerGuard {
  ~CServerGuard() { bsrvcore_server_destroy(server); }
  bsrvcore_server_t* server{nullptr};
};

unsigned short StartCServer(const CServerGuard& guard) {
  const auto port = FindFreePort();
  const auto add_listen =
      bsrvcore_server_add_listen(guard.server, "127.0.0.1", port, 1);
  if (add_listen != BSRVCORE_RESULT_OK) {
    ADD_FAILURE() << "bsrvcore_server_add_listen failed: " << add_listen;
    return 0;
  }

  const auto start = bsrvcore_server_start(guard.server);
  if (start != BSRVCORE_RESULT_OK) {
    ADD_FAILURE() << "bsrvcore_server_start failed: " << start;
    return 0;
  }
  return port;
}

}  // namespace

TEST(CBindingHttpServerTest, StatelessRouteCanSetFullResponse) {
  CServerGuard guard;
  ASSERT_EQ(BSRVCORE_RESULT_OK, bsrvcore_server_create(2, &guard.server));
  ASSERT_EQ(BSRVCORE_RESULT_OK,
            bsrvcore_server_add_route(guard.server, BSRVCORE_HTTP_METHOD_GET,
                                      "/hello", hello_handler));

  int running = 0;
  ASSERT_EQ(BSRVCORE_RESULT_OK,
            bsrvcore_server_is_running(guard.server, &running));
  EXPECT_EQ(0, running);

  const auto port = StartCServer(guard);
  ASSERT_NE(0, port);

  auto response = DoRequestWithRetry(http::verb::get, port, "/hello", "");
  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_EQ(response.body(), "hello");

  ASSERT_EQ(BSRVCORE_RESULT_OK,
            bsrvcore_server_is_running(guard.server, &running));
  EXPECT_EQ(1, running);
}

TEST(CBindingHttpServerTest, StatefulRouteCanReadHeadersAndBody) {
  CServerGuard guard;
  RouteContext route_ctx{"ctx"};

  ASSERT_EQ(BSRVCORE_RESULT_OK, bsrvcore_server_create(4, &guard.server));
  ASSERT_EQ(BSRVCORE_RESULT_OK,
            bsrvcore_server_set_header_read_expiry_ms(guard.server, 1000));
  ASSERT_EQ(BSRVCORE_RESULT_OK,
            bsrvcore_server_set_default_read_expiry_ms(guard.server, 2000));
  ASSERT_EQ(BSRVCORE_RESULT_OK,
            bsrvcore_server_set_default_write_expiry_ms(guard.server, 2000));
  ASSERT_EQ(BSRVCORE_RESULT_OK,
            bsrvcore_server_set_default_max_body_size(guard.server, 1024));
  ASSERT_EQ(BSRVCORE_RESULT_OK,
            bsrvcore_server_set_keep_alive_timeout_ms(guard.server, 3000));
  ASSERT_EQ(BSRVCORE_RESULT_OK, bsrvcore_server_add_route_with_ctx(
                                    guard.server, BSRVCORE_HTTP_METHOD_POST,
                                    "/echo", echo_handler, &route_ctx));
  ASSERT_EQ(BSRVCORE_RESULT_OK,
            bsrvcore_server_set_route_read_expiry_ms(
                guard.server, BSRVCORE_HTTP_METHOD_POST, "/echo", 1500));
  ASSERT_EQ(BSRVCORE_RESULT_OK,
            bsrvcore_server_set_route_write_expiry_ms(
                guard.server, BSRVCORE_HTTP_METHOD_POST, "/echo", 1500));
  ASSERT_EQ(BSRVCORE_RESULT_OK,
            bsrvcore_server_set_route_max_body_size(
                guard.server, BSRVCORE_HTTP_METHOD_POST, "/echo", 128));

  const auto port = StartCServer(guard);
  ASSERT_NE(0, port);

  auto response =
      DoRequestWithRetry(http::verb::post, port, "/echo", "payload",
                         [](http::request<http::string_body>& request) {
                           request.set("X-Test", "header-value");
                         });

  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_EQ(response.body(), "ctx:header-value:payload");
  EXPECT_EQ(response["X-Reply"], "ctx");
}

TEST(CBindingHttpServerTest, AspectsRespectGlobalMethodSubtreeAndTerminalOrder) {
  CServerGuard guard;
  AspectContext aspect_ctx{"preM|", "postM|"};

  ASSERT_EQ(BSRVCORE_RESULT_OK, bsrvcore_server_create(2, &guard.server));
  ASSERT_EQ(BSRVCORE_RESULT_OK, bsrvcore_server_add_global_aspect(
                                    guard.server, pre_global, post_global));
  ASSERT_EQ(BSRVCORE_RESULT_OK,
            bsrvcore_server_add_method_global_aspect_with_ctx(
                guard.server, BSRVCORE_HTTP_METHOD_GET, pre_method, post_method,
                &aspect_ctx));
  ASSERT_EQ(BSRVCORE_RESULT_OK,
            bsrvcore_server_add_route(guard.server, BSRVCORE_HTTP_METHOD_GET,
                                      "/order/leaf", order_handler));
  ASSERT_EQ(BSRVCORE_RESULT_OK, bsrvcore_server_add_route_aspect(
                                    guard.server, BSRVCORE_HTTP_METHOD_GET,
                                    "/order", pre_subtree, post_subtree));
  ASSERT_EQ(BSRVCORE_RESULT_OK, bsrvcore_server_add_terminal_aspect(
                                    guard.server, BSRVCORE_HTTP_METHOD_GET,
                                    "/order/leaf", pre_terminal,
                                    post_terminal));

  const auto port = StartCServer(guard);
  ASSERT_NE(0, port);

  auto response = DoRequestWithRetry(http::verb::get, port, "/order/leaf", "");
  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_EQ(response.body(),
            "preG|preM|preS|preT|handler|postT|postS|postM|postG|");
}

TEST(CBindingHttpServerTest, RouteAspectActsAsSubtreeAspectOnlyOnSuccess) {
  CServerGuard guard;

  ASSERT_EQ(BSRVCORE_RESULT_OK, bsrvcore_server_create(2, &guard.server));
  ASSERT_EQ(BSRVCORE_RESULT_OK,
            bsrvcore_server_add_route(guard.server, BSRVCORE_HTTP_METHOD_GET,
                                      "/tree/leaf", order_handler));
  ASSERT_EQ(BSRVCORE_RESULT_OK, bsrvcore_server_add_route_aspect(
                                    guard.server, BSRVCORE_HTTP_METHOD_GET,
                                    "/tree", pre_subtree, post_subtree));

  const auto port = StartCServer(guard);
  ASSERT_NE(0, port);

  auto child = DoRequestWithRetry(http::verb::get, port, "/tree/leaf", "");
  EXPECT_EQ(child.result(), http::status::ok);
  EXPECT_EQ(child.body(), "preS|handler|postS|");

  auto parent = DoRequestWithRetry(http::verb::get, port, "/tree", "");
  EXPECT_EQ(parent.body().find("preS|"), std::string::npos);
  EXPECT_EQ(parent.body().find("postS|"), std::string::npos);
}
