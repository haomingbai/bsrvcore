#include <gtest/gtest.h>

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/oai/completion/oai_completion.h"
#include "bsrvcore/route/http_request_method.h"
#include "test_http_client_task.h"

namespace {

namespace http = boost::beast::http;
namespace json = boost::json;
using bsrvcore::oai::completion::OaiCompletionFactory;
using bsrvcore::oai::completion::OaiCompletionInfo;
using bsrvcore::oai::completion::OaiCompletionState;
using bsrvcore::oai::completion::OaiCompletionStatus;
using bsrvcore::oai::completion::OaiMessage;
using bsrvcore::oai::completion::OaiModelInfo;
using bsrvcore::oai::completion::OaiToolDefinition;
using bsrvcore::test::ServerGuard;
using bsrvcore::test::StartServerWithRoutes;

std::shared_ptr<OaiCompletionInfo> MakeInfo(unsigned short port) {
  auto info = bsrvcore::AllocateShared<OaiCompletionInfo>();
  info->api_key = "unit-test-key";
  info->base_url = "http://127.0.0.1:" + std::to_string(port);
  return info;
}

std::shared_ptr<OaiModelInfo> MakeModelInfo() {
  auto model_info = bsrvcore::AllocateShared<OaiModelInfo>();
  model_info->model = "unit-test-model";
  return model_info;
}

std::shared_ptr<OaiCompletionState> WaitCompletion(
    boost::asio::io_context& ioc, OaiCompletionFactory& factory,
    std::shared_ptr<OaiCompletionState> state,
    std::shared_ptr<OaiModelInfo> model_info,
    const std::vector<OaiToolDefinition>& tools, int* callback_count) {
  std::promise<std::shared_ptr<OaiCompletionState>> promise;
  auto future = promise.get_future();
  bool fulfilled = false;

  auto callback = [&](std::shared_ptr<OaiCompletionState> next) {
    if (callback_count != nullptr) {
      ++(*callback_count);
    }
    if (fulfilled) {
      return;
    }
    fulfilled = true;
    promise.set_value(std::move(next));
  };

  bool started = false;
  if (tools.empty()) {
    started = factory.FetchCompletion(std::move(state), std::move(model_info),
                                      callback);
  } else {
    started = factory.FetchCompletion(std::move(state), tools,
                                      std::move(model_info), callback);
  }

  EXPECT_TRUE(started);
  ioc.run();
  return future.get();
}

}  // namespace

TEST(OaiCompletionTest, AppendMessageBuildsImmutableLocalState) {
  boost::asio::io_context ioc;
  auto info = bsrvcore::AllocateShared<OaiCompletionInfo>();
  info->api_key = "k";
  info->base_url = "http://127.0.0.1";

  OaiCompletionFactory factory(ioc.get_executor(), info);

  OaiMessage user;
  user.role = "user";
  user.message = "hello";

  auto state = factory.AppendMessage(user, nullptr);
  ASSERT_NE(state, nullptr);
  EXPECT_EQ(state->GetMessage().role, "user");
  EXPECT_EQ(state->GetMessage().message, "hello");
  EXPECT_EQ(state->GetLog().status, OaiCompletionStatus::kLocal);
  EXPECT_TRUE(state->GetLog().model.empty());
  EXPECT_EQ(state->GetModelInfo(), nullptr);
  EXPECT_EQ(state->GetPreviousState(), nullptr);
}

TEST(OaiCompletionTest, CompletionSendsFullMessageChainOrderAndModelParams) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);
  auto captured_body = bsrvcore::AllocateShared<std::string>();

  server->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kPost, "/chat/completions",
      [captured_body](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        *captured_body = task->GetRequest().body();
        task->SetField(http::field::content_type, "application/json");
        task->SetBody(
            R"({"id":"req-1","model":"unit-test-model","choices":[{"finish_reason":"stop","message":{"role":"assistant","content":"ok"}}]})");
      });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  boost::asio::io_context ioc;
  OaiCompletionFactory factory(ioc.get_executor(), MakeInfo(port));

  OaiMessage m1{"system", "you are test", {}};
  OaiMessage m2{"user", "first", {}};
  OaiMessage m3{"assistant", "second", {}};

  auto s1 = factory.AppendMessage(m1, nullptr);
  auto s2 = factory.AppendMessage(m2, s1);
  auto s3 = factory.AppendMessage(m3, s2);

  auto model_info = MakeModelInfo();
  model_info->params["temperature"] = 0.25;
  model_info->params["max_tokens"] = 64;
  model_info->params["response_format"] = json::object{{"type", "json_object"}};
  model_info->params["model"] = "wrong-model";
  model_info->params["stream"] = true;
  model_info->params["messages"] = json::array{json::object{{"role", "bad"}}};
  model_info->params["tools"] = json::array{};

  int callback_count = 0;
  auto result =
      WaitCompletion(ioc, factory, s3, model_info, {}, &callback_count);

  ASSERT_NE(result, nullptr);
  EXPECT_EQ(callback_count, 1);
  EXPECT_EQ(result->GetLog().status, OaiCompletionStatus::kSuccess);
  EXPECT_EQ(result->GetModelInfo().get(), model_info.get());

  boost::system::error_code ec;
  auto root = json::parse(*captured_body, ec);
  ASSERT_FALSE(ec);
  ASSERT_TRUE(root.is_object());

  const auto& obj = root.as_object();
  ASSERT_EQ(obj.at("model").as_string(), "unit-test-model");
  ASSERT_FALSE(obj.at("stream").as_bool());
  ASSERT_EQ(obj.at("temperature").as_double(), 0.25);
  ASSERT_EQ(obj.at("max_tokens").as_int64(), 64);
  ASSERT_EQ(obj.at("response_format").as_object().at("type").as_string(),
            "json_object");
  EXPECT_EQ(obj.if_contains("tools"), nullptr);

  const auto* messages = obj.if_contains("messages");
  ASSERT_NE(messages, nullptr);
  ASSERT_TRUE(messages->is_array());

  const auto& array = messages->as_array();
  ASSERT_EQ(array.size(), 3U);
  EXPECT_EQ(array[0].as_object().at("role").as_string(), "system");
  EXPECT_EQ(array[1].as_object().at("content").as_string(), "first");
  EXPECT_EQ(array[2].as_object().at("content").as_string(), "second");
}

TEST(OaiCompletionTest, CompletionSendsToolDefinitionsAsJsonObjects) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);
  auto captured_body = bsrvcore::AllocateShared<std::string>();

  server->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kPost, "/chat/completions",
      [captured_body](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        *captured_body = task->GetRequest().body();
        task->SetField(http::field::content_type, "application/json");
        task->SetBody(
            R"({"id":"req-tool-def","model":"unit-test-model","choices":[{"finish_reason":"stop","message":{"role":"assistant","content":"ok"}}]})");
      });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  boost::asio::io_context ioc;
  OaiCompletionFactory factory(ioc.get_executor(), MakeInfo(port));

  OaiMessage user{"user", "use tool", {}};
  auto state = factory.AppendMessage(user, nullptr);

  OaiToolDefinition tool;
  tool.name = "get_weather";
  tool.description = "Fetch weather";
  tool.parameters = {
      {"type", "object"},
      {"properties", json::object{{"city", json::object{{"type", "string"}}}}},
      {"required", json::array{"city"}},
  };

  int callback_count = 0;
  auto result = WaitCompletion(ioc, factory, state, MakeModelInfo(), {tool},
                               &callback_count);

  ASSERT_NE(result, nullptr);
  EXPECT_EQ(callback_count, 1);
  EXPECT_EQ(result->GetLog().status, OaiCompletionStatus::kSuccess);

  boost::system::error_code ec;
  auto root = json::parse(*captured_body, ec);
  ASSERT_FALSE(ec);
  ASSERT_TRUE(root.is_object());

  const auto& obj = root.as_object();
  const auto* tools_value = obj.if_contains("tools");
  ASSERT_NE(tools_value, nullptr);
  ASSERT_TRUE(tools_value->is_array());
  ASSERT_EQ(tools_value->as_array().size(), 1U);

  const auto& tool_obj = tools_value->as_array().front().as_object();
  const auto& function_obj = tool_obj.at("function").as_object();
  EXPECT_EQ(function_obj.at("name").as_string(), "get_weather");
  const auto& parameters_obj = function_obj.at("parameters").as_object();
  EXPECT_EQ(parameters_obj.at("type").as_string(), "object");
  EXPECT_EQ(parameters_obj.at("properties")
                .as_object()
                .at("city")
                .as_object()
                .at("type")
                .as_string(),
            "string");
}

TEST(OaiCompletionTest, CompletionParsesToolCalls) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);

  server->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kPost, "/chat/completions",
      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        task->SetField(http::field::content_type, "application/json");
        task->SetBody(
            R"({"id":"req-tool","model":"unit-test-model","choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","content":"","tool_calls":[{"id":"call_1","type":"function","function":{"name":"get_weather","arguments":"{\"city\":\"Beijing\"}"}}]}}]})");
      });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  boost::asio::io_context ioc;
  OaiCompletionFactory factory(ioc.get_executor(), MakeInfo(port));

  OaiMessage user{"user", "please call a tool", {}};
  auto state = factory.AppendMessage(user, nullptr);

  int callback_count = 0;
  auto result =
      WaitCompletion(ioc, factory, state, MakeModelInfo(), {}, &callback_count);

  ASSERT_NE(result, nullptr);
  EXPECT_EQ(callback_count, 1);
  EXPECT_EQ(result->GetLog().status, OaiCompletionStatus::kSuccess);
  ASSERT_EQ(result->GetMessage().tool_calls.size(), 1U);
  EXPECT_EQ(result->GetMessage().tool_calls[0].id, "call_1");
  EXPECT_EQ(result->GetMessage().tool_calls[0].name, "get_weather");
  ASSERT_TRUE(result->GetMessage().tool_calls[0].arguments.is_object());
  EXPECT_EQ(result->GetMessage()
                .tool_calls[0]
                .arguments.as_object()
                .at("city")
                .as_string(),
            "Beijing");
}

TEST(OaiCompletionTest, CompletionPreservesInvalidToolCallArgumentString) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);

  server->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kPost, "/chat/completions",
      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        task->SetField(http::field::content_type, "application/json");
        task->SetBody(
            R"({"id":"req-tool-invalid","model":"unit-test-model","choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","content":"","tool_calls":[{"id":"call_1","type":"function","function":{"name":"get_weather","arguments":"not-json"}}]}}]})");
      });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  boost::asio::io_context ioc;
  OaiCompletionFactory factory(ioc.get_executor(), MakeInfo(port));

  OaiMessage user{"user", "please call a tool", {}};
  auto state = factory.AppendMessage(user, nullptr);

  int callback_count = 0;
  auto result =
      WaitCompletion(ioc, factory, state, MakeModelInfo(), {}, &callback_count);

  ASSERT_NE(result, nullptr);
  EXPECT_EQ(callback_count, 1);
  EXPECT_EQ(result->GetLog().status, OaiCompletionStatus::kSuccess);
  ASSERT_EQ(result->GetMessage().tool_calls.size(), 1U);
  ASSERT_TRUE(result->GetMessage().tool_calls[0].arguments.is_string());
  EXPECT_EQ(result->GetMessage().tool_calls[0].arguments.as_string(),
            "not-json");
}

TEST(OaiCompletionTest, CompletionParsesReasoningContent) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);

  server->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kPost, "/chat/completions",
      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        task->SetField(http::field::content_type, "application/json");
        task->SetBody(
            R"({"id":"req-reason","model":"unit-test-model","choices":[{"finish_reason":"stop","message":{"role":"assistant","content":"OK","reasoning_content":"Because."}}]})");
      });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  boost::asio::io_context ioc;
  OaiCompletionFactory factory(ioc.get_executor(), MakeInfo(port));

  OaiMessage user{"user", "give reasoning", {}};
  auto state = factory.AppendMessage(user, nullptr);

  int callback_count = 0;
  auto result =
      WaitCompletion(ioc, factory, state, MakeModelInfo(), {}, &callback_count);

  ASSERT_NE(result, nullptr);
  EXPECT_EQ(callback_count, 1);
  EXPECT_EQ(result->GetLog().status, OaiCompletionStatus::kSuccess);
  EXPECT_EQ(result->GetMessage().message, "OK");
  EXPECT_EQ(result->GetMessage().reasoning, "Because.");
}

TEST(OaiCompletionTest, StreamAggregatesDeltaAndDone) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);

  server->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kPost, "/chat/completions",
      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        task->SetField(http::field::content_type,
                       "text/event-stream; charset=utf-8");
        task->SetBody(
            "data: "
            "{\"id\":\"stream-1\",\"model\":\"unit-test-model\",\"choices\":[{"
            "\"delta\":{\"content\":\"Hel\"},\"finish_reason\":null}]}\n\n"
            "data: "
            "{\"id\":\"stream-1\",\"model\":\"unit-test-model\",\"choices\":[{"
            "\"delta\":{\"content\":\"lo\"},\"finish_reason\":null}]}\n\n"
            "data: "
            "{\"id\":\"stream-1\",\"model\":\"unit-test-model\",\"choices\":[{"
            "\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
            "data: [DONE]\n\n");
      });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  boost::asio::io_context ioc;
  OaiCompletionFactory factory(ioc.get_executor(), MakeInfo(port));

  OaiMessage user{"user", "stream me", {}};
  auto state = factory.AppendMessage(user, nullptr);
  auto model_info = MakeModelInfo();

  std::vector<std::string> deltas;
  int done_count = 0;
  std::promise<std::shared_ptr<OaiCompletionState>> done_promise;
  auto done_future = done_promise.get_future();
  bool fulfilled = false;

  const bool started = factory.FetchStreamCompletion(
      state, model_info,
      [&](std::shared_ptr<OaiCompletionState> done_state) {
        ++done_count;
        if (fulfilled) {
          return;
        }
        fulfilled = true;
        done_promise.set_value(std::move(done_state));
      },
      [&](const std::string& delta) { deltas.push_back(delta); });

  ASSERT_TRUE(started);
  ioc.run();

  auto result = done_future.get();
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(done_count, 1);
  EXPECT_EQ(result->GetLog().status, OaiCompletionStatus::kSuccess);
  EXPECT_EQ(result->GetModelInfo().get(), model_info.get());
  EXPECT_EQ(result->GetMessage().message, "Hello");
  ASSERT_EQ(deltas.size(), 2U);
  EXPECT_EQ(deltas[0], "Hel");
  EXPECT_EQ(deltas[1], "lo");
}

TEST(OaiCompletionTest, StreamAggregatesReasoningWithoutOnDelta) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);

  server->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kPost, "/chat/completions",
      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        task->SetField(http::field::content_type,
                       "text/event-stream; charset=utf-8");
        task->SetBody(
            "data: "
            "{\"id\":\"stream-reason\",\"model\":\"unit-test-model\","
            "\"choices\":[{\"delta\":{\"reasoning_content\":\"A\"},\"finish_"
            "reason\":null}]}\n\n"
            "data: "
            "{\"id\":\"stream-reason\",\"model\":\"unit-test-model\","
            "\"choices\":[{\"delta\":{\"content\":\"Hel\"},\"finish_reason\":"
            "null}]}\n\n"
            "data: "
            "{\"id\":\"stream-reason\",\"model\":\"unit-test-model\","
            "\"choices\":[{\"delta\":{\"reasoning_content\":\"B\"},\"finish_"
            "reason\":null}]}\n\n"
            "data: "
            "{\"id\":\"stream-reason\",\"model\":\"unit-test-model\","
            "\"choices\":[{\"delta\":{\"content\":\"lo\"},\"finish_reason\":"
            "null}]}\n\n"
            "data: "
            "{\"id\":\"stream-reason\",\"model\":\"unit-test-model\","
            "\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
            "data: [DONE]\n\n");
      });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  boost::asio::io_context ioc;
  OaiCompletionFactory factory(ioc.get_executor(), MakeInfo(port));

  OaiMessage user{"user", "stream reasoning", {}};
  auto state = factory.AppendMessage(user, nullptr);

  std::vector<std::string> deltas;
  int done_count = 0;
  std::promise<std::shared_ptr<OaiCompletionState>> done_promise;
  auto done_future = done_promise.get_future();
  bool fulfilled = false;

  const bool started = factory.FetchStreamCompletion(
      state, MakeModelInfo(),
      [&](std::shared_ptr<OaiCompletionState> done_state) {
        ++done_count;
        if (fulfilled) {
          return;
        }
        fulfilled = true;
        done_promise.set_value(std::move(done_state));
      },
      [&](const std::string& delta) { deltas.push_back(delta); });

  ASSERT_TRUE(started);
  ioc.run();

  auto result = done_future.get();
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(done_count, 1);
  EXPECT_EQ(result->GetLog().status, OaiCompletionStatus::kSuccess);
  EXPECT_EQ(result->GetMessage().message, "Hello");
  EXPECT_EQ(result->GetMessage().reasoning, "AB");
  ASSERT_EQ(deltas.size(), 2U);
  EXPECT_EQ(deltas[0], "Hel");
  EXPECT_EQ(deltas[1], "lo");
}

TEST(OaiCompletionTest, StreamCallsReasoningDeltaCallbackWhenProvided) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);

  server->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kPost, "/chat/completions",
      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        task->SetField(http::field::content_type,
                       "text/event-stream; charset=utf-8");
        task->SetBody(
            "data: "
            "{\"id\":\"stream-reason-cb\",\"model\":\"unit-test-model\","
            "\"choices\":[{\"delta\":{\"reasoning_content\":\"A\"},\"finish_"
            "reason\":null}]}\n\n"
            "data: "
            "{\"id\":\"stream-reason-cb\",\"model\":\"unit-test-model\","
            "\"choices\":[{\"delta\":{\"content\":\"Hel\"},\"finish_reason\":"
            "null}]}\n\n"
            "data: "
            "{\"id\":\"stream-reason-cb\",\"model\":\"unit-test-model\","
            "\"choices\":[{\"delta\":{\"reasoning_content\":\"B\"},\"finish_"
            "reason\":null}]}\n\n"
            "data: "
            "{\"id\":\"stream-reason-cb\",\"model\":\"unit-test-model\","
            "\"choices\":[{\"delta\":{\"content\":\"lo\"},\"finish_reason\":"
            "null}]}\n\n"
            "data: "
            "{\"id\":\"stream-reason-cb\",\"model\":\"unit-test-model\","
            "\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
            "data: [DONE]\n\n");
      });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  boost::asio::io_context ioc;
  OaiCompletionFactory factory(ioc.get_executor(), MakeInfo(port));

  OaiMessage user{"user", "stream reasoning callback", {}};
  auto state = factory.AppendMessage(user, nullptr);

  std::vector<std::string> deltas;
  std::vector<std::string> reasoning_deltas;
  int done_count = 0;
  std::promise<std::shared_ptr<OaiCompletionState>> done_promise;
  auto done_future = done_promise.get_future();
  bool fulfilled = false;

  const bool started = factory.FetchStreamCompletion(
      state, MakeModelInfo(),
      [&](std::shared_ptr<OaiCompletionState> done_state) {
        ++done_count;
        if (fulfilled) {
          return;
        }
        fulfilled = true;
        done_promise.set_value(std::move(done_state));
      },
      [&](const std::string& delta) { deltas.push_back(delta); },
      [&](const std::string& reasoning_delta) {
        reasoning_deltas.push_back(reasoning_delta);
      });

  ASSERT_TRUE(started);
  ioc.run();

  auto result = done_future.get();
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(done_count, 1);
  EXPECT_EQ(result->GetLog().status, OaiCompletionStatus::kSuccess);
  EXPECT_EQ(result->GetMessage().message, "Hello");
  EXPECT_EQ(result->GetMessage().reasoning, "AB");

  ASSERT_EQ(deltas.size(), 2U);
  EXPECT_EQ(deltas[0], "Hel");
  EXPECT_EQ(deltas[1], "lo");

  ASSERT_EQ(reasoning_deltas.size(), 2U);
  EXPECT_EQ(reasoning_deltas[0], "A");
  EXPECT_EQ(reasoning_deltas[1], "B");
}

TEST(OaiCompletionTest, FailureStillCallsCompletionCallbackAndKeepsModelInfo) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);

  server->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kPost, "/chat/completions",
      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        task->GetResponse().result(http::status::internal_server_error);
        task->SetField(http::field::content_type, "application/json");
        task->SetBody(R"({"error":{"message":"mock failure"}})");
      });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  boost::asio::io_context ioc;
  OaiCompletionFactory factory(ioc.get_executor(), MakeInfo(port));

  OaiMessage user{"user", "trigger failure", {}};
  auto state = factory.AppendMessage(user, nullptr);
  auto model_info = MakeModelInfo();

  int callback_count = 0;
  auto result =
      WaitCompletion(ioc, factory, state, model_info, {}, &callback_count);

  ASSERT_NE(result, nullptr);
  EXPECT_EQ(callback_count, 1);
  EXPECT_EQ(result->GetLog().status, OaiCompletionStatus::kFail);
  EXPECT_EQ(result->GetModelInfo().get(), model_info.get());
  EXPECT_EQ(result->GetLog().http_status_code,
            static_cast<int>(http::status::internal_server_error));
  EXPECT_NE(result->GetLog().error_message.find("mock failure"),
            std::string::npos);
}
