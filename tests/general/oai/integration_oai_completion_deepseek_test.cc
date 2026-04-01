#include <gtest/gtest.h>

#include <boost/json.hpp>
#include <filesystem>
#include <fstream>
#include <future>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/oai/completion/oai_completion.h"

namespace {

namespace json = boost::json;
using bsrvcore::oai::completion::OaiCompletionFactory;
using bsrvcore::oai::completion::OaiCompletionInfo;
using bsrvcore::oai::completion::OaiCompletionState;
using bsrvcore::oai::completion::OaiCompletionStatus;
using bsrvcore::oai::completion::OaiMessage;
using bsrvcore::oai::completion::OaiModelInfo;

struct DeepSeekConfig {
  OaiCompletionInfo info;
  OaiModelInfo model_info;
};

std::optional<std::filesystem::path> FindConfigPath() {
  namespace fs = std::filesystem;
  fs::path current = fs::current_path();

  for (int i = 0; i < 8; ++i) {
    fs::path candidate =
        current / ".artifacts" / "secrets" / "oai_completion_test_config.json";
    if (fs::exists(candidate)) {
      return candidate;
    }

    if (!current.has_parent_path()) {
      break;
    }
    current = current.parent_path();
  }

  return std::nullopt;
}

std::optional<DeepSeekConfig> LoadDeepSeekConfig() {
  const auto config_path = FindConfigPath();
  if (!config_path.has_value()) {
    return std::nullopt;
  }

  std::ifstream file(config_path->string());
  if (!file.is_open()) {
    return std::nullopt;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();

  json::error_code ec;
  json::value root = json::parse(buffer.str(), ec);
  if (ec || !root.is_object()) {
    return std::nullopt;
  }

  const auto& obj = root.as_object();
  const json::value* provider = obj.if_contains("provider");
  if (provider == nullptr || !provider->is_string() ||
      provider->as_string() != "deepseek") {
    return std::nullopt;
  }

  const json::value* base_url = obj.if_contains("base_url");
  const json::value* api_key = obj.if_contains("api_key");
  const json::value* model = obj.if_contains("model");
  if (base_url == nullptr || api_key == nullptr || model == nullptr ||
      !base_url->is_string() || !api_key->is_string() || !model->is_string()) {
    return std::nullopt;
  }

  DeepSeekConfig config;
  config.info.base_url = std::string(base_url->as_string().c_str());
  config.info.api_key = std::string(api_key->as_string().c_str());
  config.model_info.model = std::string(model->as_string().c_str());

  if (config.info.base_url.empty() || config.info.api_key.empty() ||
      config.model_info.model.empty()) {
    return std::nullopt;
  }

  return config;
}

std::shared_ptr<OaiCompletionState> WaitCompletion(
    boost::asio::io_context& ioc, OaiCompletionFactory& factory,
    std::shared_ptr<OaiCompletionState> state,
    std::shared_ptr<OaiModelInfo> model_info) {
  std::promise<std::shared_ptr<OaiCompletionState>> promise;
  auto future = promise.get_future();

  bool fulfilled = false;
  const bool started =
      factory.FetchCompletion(std::move(state), std::move(model_info),
                              [&](std::shared_ptr<OaiCompletionState> next) {
                                if (fulfilled) {
                                  return;
                                }
                                fulfilled = true;
                                promise.set_value(std::move(next));
                              });
  EXPECT_TRUE(started);

  ioc.run();
  return future.get();
}

}  // namespace

TEST(OaiCompletionIntegrationTest, DeepSeekCompletionSuccessWhenConfigPresent) {
  auto config = LoadDeepSeekConfig();
  if (!config.has_value()) {
    GTEST_SKIP()
        << "Missing valid .artifacts/secrets/oai_completion_test_config.json";
  }

  boost::asio::io_context ioc;
  auto info = bsrvcore::AllocateShared<OaiCompletionInfo>(config->info);
  auto model_info = bsrvcore::AllocateShared<OaiModelInfo>(config->model_info);
  OaiCompletionFactory factory(ioc.get_executor(), info);

  OaiMessage user;
  user.role = "user";
  user.message = "Reply with only OK.";

  auto state = factory.AppendMessage(user, nullptr);
  auto result = WaitCompletion(ioc, factory, state, model_info);

  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->GetLog().status, OaiCompletionStatus::kSuccess);
  EXPECT_EQ(result->GetModelInfo().get(), model_info.get());
  EXPECT_FALSE(result->GetMessage().message.empty());
  EXPECT_TRUE(result->GetLog().error_message.empty());
}

TEST(OaiCompletionIntegrationTest,
     DeepSeekStreamCompletionSuccessWhenConfigPresent) {
  auto config = LoadDeepSeekConfig();
  if (!config.has_value()) {
    GTEST_SKIP()
        << "Missing valid .artifacts/secrets/oai_completion_test_config.json";
  }

  boost::asio::io_context ioc;
  auto info = bsrvcore::AllocateShared<OaiCompletionInfo>(config->info);
  auto model_info = bsrvcore::AllocateShared<OaiModelInfo>(config->model_info);
  OaiCompletionFactory factory(ioc.get_executor(), info);

  OaiMessage user;
  user.role = "user";
  user.message = "Please answer in one short sentence.";

  auto state = factory.AppendMessage(user, nullptr);

  std::vector<std::string> deltas;
  std::promise<std::shared_ptr<OaiCompletionState>> done_promise;
  auto done_future = done_promise.get_future();
  bool fulfilled = false;

  const bool started = factory.FetchStreamCompletion(
      state, model_info,
      [&](std::shared_ptr<OaiCompletionState> done_state) {
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
  EXPECT_EQ(result->GetLog().status, OaiCompletionStatus::kSuccess);
  EXPECT_EQ(result->GetModelInfo().get(), model_info.get());
  EXPECT_FALSE(result->GetMessage().message.empty());
  EXPECT_GT(result->GetLog().delta_count, 0U);
  EXPECT_TRUE(result->GetLog().error_message.empty());
}
