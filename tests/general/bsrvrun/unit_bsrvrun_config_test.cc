#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "config_loader.h"

namespace {

std::filesystem::path WriteTempFile(const std::string& content,
                                    const std::string& filename) {
  const auto path = std::filesystem::temp_directory_path() / filename;
  std::ofstream out(path);
  out << content;
  out.close();
  return path;
}

}  // namespace

TEST(BsrvRunConfigTest, ResolveCliPath) {
  const auto path = WriteTempFile("listeners: []\n", "bsrvrun_test_cli.yaml");

  const auto resolved = bsrvcore::runtime::ResolveConfigPath(path.string());
  EXPECT_EQ(resolved, path.string());

  std::filesystem::remove(path);
}

TEST(BsrvRunConfigTest, ParseValidConfig) {
  const auto path = WriteTempFile(
      "server:\n"
      "  thread_count: 2\n"
      "  has_max_connection: true\n"
      "  max_connection: 128\n"
      "  executor:\n"
      "    core_thread_num: 3\n"
      "    max_thread_num: 6\n"
      "    fast_queue_capacity: 128\n"
      "    thread_clean_interval: 50000\n"
      "    task_scan_interval: 80\n"
      "    suspend_time: 1\n"
      "listeners:\n"
      "  - address: \"127.0.0.1\"\n"
      "    port: 8081\n"
      "    io_threads: 2\n"
      "services:\n"
      "  - slot: 2\n"
      "    factory: \"/tmp/service.so\"\n"
      "    params:\n"
      "      body: \"svc|\"\n"
      "global:\n"
      "  aspects:\n"
      "    - factory: \"/tmp/aspect.so\"\n"
      "      params:\n"
      "        pre: \"gpre|\"\n"
      "routes:\n"
      "  - method: \"GET\"\n"
      "    path: \"/health\"\n"
      "    ignore_default_route: true\n"
      "    cpu: true\n"
      "    handler:\n"
      "      factory: \"/tmp/handler.so\"\n"
      "      params:\n"
      "        body: \"ok|\"\n",
      "bsrvrun_test_valid.yaml");

  const auto config = bsrvcore::runtime::LoadConfigFromFile(path.string());
  EXPECT_EQ(config.thread_count, 2u);
  EXPECT_TRUE(config.has_max_connection);
  EXPECT_EQ(config.max_connection, 128u);
  EXPECT_TRUE(config.executor.configured);
  EXPECT_EQ(config.executor.core_thread_num, 3u);
  EXPECT_EQ(config.executor.max_thread_num, 6u);
  EXPECT_EQ(config.executor.fast_queue_capacity, 128u);
  EXPECT_EQ(config.executor.thread_clean_interval, 50000u);
  EXPECT_EQ(config.executor.task_scan_interval, 80u);
  EXPECT_EQ(config.executor.suspend_time, 1u);
  ASSERT_EQ(config.listeners.size(), 1u);
  EXPECT_EQ(config.listeners[0].address, "127.0.0.1");
  EXPECT_EQ(config.listeners[0].port, 8081);
  EXPECT_EQ(config.listeners[0].io_threads, 2u);

  ASSERT_EQ(config.services.size(), 1u);
  EXPECT_EQ(config.services[0].slot, 2u);
  EXPECT_EQ(config.services[0].factory.library, "/tmp/service.so");
  EXPECT_EQ(config.services[0].factory.params.at("body"), "svc|");

  ASSERT_EQ(config.global.aspects.size(), 1u);
  EXPECT_EQ(config.global.aspects[0].library, "/tmp/aspect.so");

  ASSERT_EQ(config.routes.size(), 1u);
  EXPECT_TRUE(config.routes[0].ignore_default_route);
  EXPECT_TRUE(config.routes[0].cpu);
  EXPECT_EQ(config.routes[0].path, "/health");
  EXPECT_EQ(config.routes[0].handler.params.at("body"), "ok|");

  std::filesystem::remove(path);
}

TEST(BsrvRunConfigTest, RejectUnsupportedMethod) {
  const auto path = WriteTempFile(
      "listeners:\n"
      "  - address: \"127.0.0.1\"\n"
      "    port: 8081\n"
      "routes:\n"
      "  - method: \"OPTIONS\"\n"
      "    path: \"/x\"\n"
      "    handler:\n"
      "      factory: \"/tmp/handler.so\"\n",
      "bsrvrun_test_invalid_method.yaml");

  EXPECT_THROW((void)bsrvcore::runtime::LoadConfigFromFile(path.string()),
               std::runtime_error);

  std::filesystem::remove(path);
}

TEST(BsrvRunConfigTest, RejectDuplicateServiceSlot) {
  const auto path = WriteTempFile(
      "listeners:\n"
      "  - address: \"127.0.0.1\"\n"
      "    port: 8081\n"
      "services:\n"
      "  - slot: 0\n"
      "    factory: \"/tmp/service-a.so\"\n"
      "  - slot: 0\n"
      "    factory: \"/tmp/service-b.so\"\n"
      "routes:\n"
      "  - method: \"GET\"\n"
      "    path: \"/x\"\n"
      "    handler:\n"
      "      factory: \"/tmp/handler.so\"\n",
      "bsrvrun_test_duplicate_service_slot.yaml");

  EXPECT_THROW((void)bsrvcore::runtime::LoadConfigFromFile(path.string()),
               std::runtime_error);

  std::filesystem::remove(path);
}

TEST(BsrvRunConfigTest, RejectMissingServiceSlot) {
  const auto path = WriteTempFile(
      "listeners:\n"
      "  - address: \"127.0.0.1\"\n"
      "    port: 8081\n"
      "services:\n"
      "  - factory: \"/tmp/service.so\"\n"
      "routes:\n"
      "  - method: \"GET\"\n"
      "    path: \"/x\"\n"
      "    handler:\n"
      "      factory: \"/tmp/handler.so\"\n",
      "bsrvrun_test_missing_service_slot.yaml");

  EXPECT_THROW((void)bsrvcore::runtime::LoadConfigFromFile(path.string()),
               std::runtime_error);

  std::filesystem::remove(path);
}

TEST(BsrvRunConfigTest, RejectExecutorMaxLessThanCore) {
  const auto path = WriteTempFile(
      "server:\n"
      "  thread_count: 2\n"
      "  executor:\n"
      "    core_thread_num: 4\n"
      "    max_thread_num: 2\n"
      "listeners:\n"
      "  - address: \"127.0.0.1\"\n"
      "    port: 8081\n"
      "routes:\n"
      "  - method: \"GET\"\n"
      "    path: \"/x\"\n"
      "    handler:\n"
      "      factory: \"/tmp/handler.so\"\n",
      "bsrvrun_test_invalid_executor.yaml");

  EXPECT_THROW((void)bsrvcore::runtime::LoadConfigFromFile(path.string()),
               std::runtime_error);

  std::filesystem::remove(path);
}

TEST(BsrvRunConfigTest, RejectMissingMaxConnectionWhenEnabled) {
  const auto path = WriteTempFile(
      "server:\n"
      "  thread_count: 2\n"
      "  has_max_connection: true\n"
      "listeners:\n"
      "  - address: \"127.0.0.1\"\n"
      "    port: 8081\n"
      "routes:\n"
      "  - method: \"GET\"\n"
      "    path: \"/x\"\n"
      "    handler:\n"
      "      factory: \"/tmp/handler.so\"\n",
      "bsrvrun_test_invalid_max_connection.yaml");

  EXPECT_THROW((void)bsrvcore::runtime::LoadConfigFromFile(path.string()),
               std::runtime_error);

  std::filesystem::remove(path);
}
