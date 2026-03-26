#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "bsrvcore/core/http_server.h"
#include "config_loader.h"
#include "plugin_loader.h"
#include "server_builder.h"
#include "test_http_client_task.h"

namespace {

using bsrvcore::test::DoRequestWithRetry;
namespace http = boost::beast::http;

std::string EscapePath(std::string value) {
  for (std::size_t pos = 0; (pos = value.find('\\', pos)) != std::string::npos;
       pos += 2) {
    value.replace(pos, 1, "\\\\");
  }
  return value;
}

std::filesystem::path WriteConfig(const std::string& body,
                                  const std::string& filename) {
  const auto path = std::filesystem::temp_directory_path() / filename;
  std::ofstream out(path);
  out << body;
  out.close();
  return path;
}

}  // namespace

TEST(BsrvRunRuntimeIntegrationTest, AppliesGlobalAndRouteAspects) {
  const unsigned short port = bsrvcore::test::FindFreePort();

  const std::string yaml =
      "server:\n"
      "  thread_count: 2\n"
      "listeners:\n"
      "  - address: \"127.0.0.1\"\n"
      "    port: " +
      std::to_string(port) +
      "\n"
      "global:\n"
      "  aspects:\n"
      "    - factory: \"" +
      EscapePath(BSRVRUN_TEST_ASPECT_PLUGIN) +
      "\"\n"
      "      params:\n"
      "        pre: \"gpre|\"\n"
      "        post: \"gpost|\"\n"
      "routes:\n"
      "  - method: \"GET\"\n"
      "    path: \"/demo\"\n"
      "    handler:\n"
      "      factory: \"" +
      EscapePath(BSRVRUN_TEST_HANDLER_PLUGIN) +
      "\"\n"
      "      params:\n"
      "        body: \"handler|\"\n"
      "    aspects:\n"
      "      - factory: \"" +
      EscapePath(BSRVRUN_TEST_ASPECT_PLUGIN) +
      "\"\n"
      "        params:\n"
      "          pre: \"rpre|\"\n"
      "          post: \"rpost|\"\n";

  const auto config_path =
      WriteConfig(yaml, "bsrvrun_runtime_integration_1.yaml");

  const auto config =
      bsrvcore::runtime::LoadConfigFromFile(config_path.string());
  bsrvcore::runtime::PluginLoader loader;
  auto server =
      bsrvcore::AllocateUnique<bsrvcore::HttpServer>(config.thread_count);
  bsrvcore::runtime::ApplyConfigToServer(config, &loader, server.get());

  ASSERT_TRUE(server->Start(1));
  auto res = DoRequestWithRetry(http::verb::get, port, "/demo", "");
  server->Stop();
  server.reset();

  EXPECT_EQ(res.result(), http::status::ok);
  EXPECT_EQ(res.body(), "gpre|rpre|handler|rpost|gpost|");

  std::filesystem::remove(config_path);
}

TEST(BsrvRunRuntimeIntegrationTest, IgnoreDefaultRouteMapsToExclusiveRoute) {
  const unsigned short port = bsrvcore::test::FindFreePort();

  const std::string yaml =
      "server:\n"
      "  thread_count: 2\n"
      "listeners:\n"
      "  - address: \"127.0.0.1\"\n"
      "    port: " +
      std::to_string(port) +
      "\n"
      "routes:\n"
      "  - method: \"GET\"\n"
      "    path: \"/static\"\n"
      "    ignore_default_route: true\n"
      "    handler:\n"
      "      factory: \"" +
      EscapePath(BSRVRUN_TEST_HANDLER_PLUGIN) +
      "\"\n"
      "      params:\n"
      "        body: \"exclusive|\"\n"
      "  - method: \"GET\"\n"
      "    path: \"/static/{file}\"\n"
      "    handler:\n"
      "      factory: \"" +
      EscapePath(BSRVRUN_TEST_HANDLER_PLUGIN) +
      "\"\n"
      "      params:\n"
      "        body: \"param|\"\n";

  const auto config_path =
      WriteConfig(yaml, "bsrvrun_runtime_integration_2.yaml");

  const auto config =
      bsrvcore::runtime::LoadConfigFromFile(config_path.string());
  bsrvcore::runtime::PluginLoader loader;
  auto server =
      bsrvcore::AllocateUnique<bsrvcore::HttpServer>(config.thread_count);
  bsrvcore::runtime::ApplyConfigToServer(config, &loader, server.get());

  ASSERT_TRUE(server->Start(1));
  auto res = DoRequestWithRetry(http::verb::get, port, "/static/abc", "");
  server->Stop();
  server.reset();

  EXPECT_EQ(res.result(), http::status::ok);
  EXPECT_EQ(res.body(), "exclusive|");

  std::filesystem::remove(config_path);
}
