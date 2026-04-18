/**
 * @file integration_bsrvrun_runtime_test.cc
 * @brief Integration tests for YAML loading + plugin-backed server assembly.
 *
 * Focus:
 * - global and route-local plugin aspects
 * - handler factory parameter wiring
 * - YAML flags that map onto core routing behavior
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "bsrvcore/core/http_server.h"
#include "bsrvrun_plugins/test_service_api.h"
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

std::string ThreadIdToString(std::thread::id id) {
  std::ostringstream oss;
  oss << id;
  return oss.str();
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

  ASSERT_TRUE(server->Start());
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

  ASSERT_TRUE(server->Start());
  auto res = DoRequestWithRetry(http::verb::get, port, "/static/abc", "");
  server->Stop();
  server.reset();

  EXPECT_EQ(res.result(), http::status::ok);
  EXPECT_EQ(res.body(), "exclusive|");

  std::filesystem::remove(config_path);
}

TEST(BsrvRunRuntimeIntegrationTest,
     LoadsConfiguredServicesIntoSlotsAndDestroysThemOnCleanup) {
  const unsigned short port = bsrvcore::test::FindFreePort();
  const auto destroy_marker = std::filesystem::temp_directory_path() /
                              "bsrvrun_runtime_service_destroy_marker.txt";
  std::filesystem::remove(destroy_marker);

  const std::string yaml =
      "server:\n"
      "  thread_count: 2\n"
      "listeners:\n"
      "  - address: \"127.0.0.1\"\n"
      "    port: " +
      std::to_string(port) +
      "\n"
      "services:\n"
      "  - slot: 2\n"
      "    factory: \"" +
      EscapePath(BSRVRUN_TEST_SERVICE_PLUGIN) +
      "\"\n"
      "    params:\n"
      "      body: \"service|\"\n"
      "      destroy_marker: \"" +
      EscapePath(destroy_marker.string()) +
      "\"\n"
      "routes:\n"
      "  - method: \"GET\"\n"
      "    path: \"/service\"\n"
      "    handler:\n"
      "      factory: \"" +
      EscapePath(BSRVRUN_TEST_HANDLER_PLUGIN) +
      "\"\n"
      "      params:\n"
      "        service_slot: \"2\"\n"
      "        body: \"handler|\"\n";

  const auto config_path =
      WriteConfig(yaml, "bsrvrun_runtime_integration_service.yaml");

  const auto config =
      bsrvcore::runtime::LoadConfigFromFile(config_path.string());
  bsrvcore::runtime::PluginLoader loader;
  auto server =
      bsrvcore::AllocateUnique<bsrvcore::HttpServer>(config.thread_count);
  bsrvcore::runtime::ApplyConfigToServer(config, &loader, server.get());

  auto service = server->GetServiceProvider(2).Get<TestServiceData>();
  ASSERT_NE(service, nullptr);
  EXPECT_EQ(service->body, "service|");

  ASSERT_TRUE(server->Start());
  auto res = DoRequestWithRetry(http::verb::get, port, "/service", "");
  server->Stop();
  EXPECT_NO_THROW(loader.DestroyServices(server.get()));
  EXPECT_EQ(server->GetServiceProvider(2).pointer, nullptr);
  server.reset();

  EXPECT_EQ(res.result(), http::status::ok);
  EXPECT_EQ(res.body(), "service|handler|");

  std::ifstream in(destroy_marker);
  std::string destroy_body;
  std::getline(in, destroy_body);
  EXPECT_EQ(destroy_body, "service|");

  std::filesystem::remove(config_path);
  std::filesystem::remove(destroy_marker);
}

TEST(BsrvRunRuntimeIntegrationTest, CpuRouteRunsOnWorkerPool) {
  const unsigned short port = bsrvcore::test::FindFreePort();

  const std::string yaml =
      "server:\n"
      "  thread_count: 1\n"
      "listeners:\n"
      "  - address: \"127.0.0.1\"\n"
      "    port: " +
      std::to_string(port) +
      "\n"
      "routes:\n"
      "  - method: \"GET\"\n"
      "    path: \"/cpu\"\n"
      "    cpu: true\n"
      "    handler:\n"
      "      factory: \"" +
      EscapePath(BSRVRUN_TEST_HANDLER_PLUGIN) +
      "\"\n"
      "      params:\n"
      "        body: \"cpu|\"\n"
      "        thread_id: \"true\"\n";

  const auto config_path =
      WriteConfig(yaml, "bsrvrun_runtime_integration_cpu.yaml");

  const auto config =
      bsrvcore::runtime::LoadConfigFromFile(config_path.string());
  bsrvcore::runtime::PluginLoader loader;
  auto server =
      bsrvcore::AllocateUnique<bsrvcore::HttpServer>(config.thread_count);
  bsrvcore::runtime::ApplyConfigToServer(config, &loader, server.get());

  ASSERT_TRUE(server->Start());

  auto io_promise = bsrvcore::AllocateShared<std::promise<std::thread::id>>();
  auto worker_promise =
      bsrvcore::AllocateShared<std::promise<std::thread::id>>();
  auto io_future = io_promise->get_future();
  auto worker_future = worker_promise->get_future();

  server->PostToIoContext(
      [io_promise] { io_promise->set_value(std::this_thread::get_id()); });
  server->Post([worker_promise] {
    worker_promise->set_value(std::this_thread::get_id());
  });

  ASSERT_EQ(io_future.wait_for(std::chrono::seconds(10)),
            std::future_status::ready);
  ASSERT_EQ(worker_future.wait_for(std::chrono::seconds(10)),
            std::future_status::ready);

  const auto io_thread_id = io_future.get();
  const auto worker_thread_id = worker_future.get();

  const auto res = DoRequestWithRetry(http::verb::get, port, "/cpu", "");
  server->Stop();
  server.reset();

  EXPECT_EQ(res.result(), http::status::ok);
  EXPECT_EQ(res.body(), "cpu|" + ThreadIdToString(worker_thread_id));
  EXPECT_NE(res.body(), "cpu|" + ThreadIdToString(io_thread_id));

  std::filesystem::remove(config_path);
}

TEST(BsrvRunRuntimeIntegrationTest,
     CpuRouteKeepsAspectsOnIoThreadAndHandlerOnWorkerPool) {
  const unsigned short port = bsrvcore::test::FindFreePort();

  const std::string yaml =
      "server:\n"
      "  thread_count: 1\n"
      "listeners:\n"
      "  - address: \"127.0.0.1\"\n"
      "    port: " +
      std::to_string(port) +
      "\n"
      "routes:\n"
      "  - method: \"GET\"\n"
      "    path: \"/cpu-lifecycle\"\n"
      "    cpu: true\n"
      "    handler:\n"
      "      factory: \"" +
      EscapePath(BSRVRUN_TEST_HANDLER_PLUGIN) +
      "\"\n"
      "      params:\n"
      "        body: \"handler|\"\n"
      "        thread_id: \"true\"\n"
      "    aspects:\n"
      "      - factory: \"" +
      EscapePath(BSRVRUN_TEST_ASPECT_PLUGIN) +
      "\"\n"
      "        params:\n"
      "          pre: \"pre|\"\n"
      "          post: \"post|\"\n"
      "          thread_id: \"true\"\n";

  const auto config_path =
      WriteConfig(yaml, "bsrvrun_runtime_integration_cpu_lifecycle.yaml");

  const auto config =
      bsrvcore::runtime::LoadConfigFromFile(config_path.string());
  bsrvcore::runtime::PluginLoader loader;
  auto server =
      bsrvcore::AllocateUnique<bsrvcore::HttpServer>(config.thread_count);
  bsrvcore::runtime::ApplyConfigToServer(config, &loader, server.get());

  ASSERT_TRUE(server->Start());

  auto io_promise = bsrvcore::AllocateShared<std::promise<std::thread::id>>();
  auto worker_promise =
      bsrvcore::AllocateShared<std::promise<std::thread::id>>();
  auto io_future = io_promise->get_future();
  auto worker_future = worker_promise->get_future();

  server->PostToIoContext(
      [io_promise] { io_promise->set_value(std::this_thread::get_id()); });
  server->Post([worker_promise] {
    worker_promise->set_value(std::this_thread::get_id());
  });

  ASSERT_EQ(io_future.wait_for(std::chrono::seconds(10)),
            std::future_status::ready);
  ASSERT_EQ(worker_future.wait_for(std::chrono::seconds(10)),
            std::future_status::ready);

  const auto io_thread = ThreadIdToString(io_future.get());
  const auto worker_thread = ThreadIdToString(worker_future.get());

  const auto res =
      DoRequestWithRetry(http::verb::get, port, "/cpu-lifecycle", "");
  server->Stop();
  server.reset();

  EXPECT_EQ(res.result(), http::status::ok);
  EXPECT_EQ(res.body(), "pre|" + io_thread + "handler|" + worker_thread +
                            "post|" + io_thread);

  std::filesystem::remove(config_path);
}
