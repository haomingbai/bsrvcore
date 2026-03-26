/**
 * @file bsrvrun_main.cc
 * @brief Entry point of runtime web container executable.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-16
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <atomic>
#include <boost/program_options.hpp>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "bsrvcore/core/http_server.h"
#include "config_loader.h"
#include "plugin_loader.h"
#include "server_builder.h"

namespace bsrvcore::runtime {

namespace {

std::atomic<bool> g_stop_requested = false;

void HandleSignal(int /*signal*/) { g_stop_requested.store(true); }

void PrintUsage(const char* argv0) {
  std::cout << "Usage: " << argv0 << " [-c <config.yaml>]" << std::endl;
}

std::optional<std::string> ParseConfigPath(int argc, char** argv) {
  namespace po = boost::program_options;

  po::options_description options("bsrvrun options");
  options.add_options()("help,h", "show help")(
      "config,c", po::value<std::string>(), "config file path");

  po::variables_map vm;
  try {
    const auto parsed =
        po::command_line_parser(argc, argv).options(options).run();
    po::store(parsed, vm);
    po::notify(vm);
  } catch (const po::error& e) {
    throw std::runtime_error(e.what());
  }

  if (vm.count("help") != 0U) {
    PrintUsage(argv[0]);
    std::cout << options << std::endl;
    std::exit(0);
  }

  if (vm.count("config") != 0U) {
    return vm["config"].as<std::string>();
  }

  return std::nullopt;
}

}  // namespace

int RunMain(int argc, char** argv) {
  try {
    const std::optional<std::string> cli_path = ParseConfigPath(argc, argv);
    const std::string config_path = ResolveConfigPath(cli_path);
    const ServerConfig config = LoadConfigFromFile(config_path);

    bsrvcore::HttpServerExecutorOptions executor_options;
    if (config.executor.configured) {
      executor_options.core_thread_num = config.executor.core_thread_num;
      executor_options.max_thread_num = config.executor.max_thread_num;
      executor_options.fast_queue_capacity =
          config.executor.fast_queue_capacity;
      executor_options.thread_clean_interval =
          config.executor.thread_clean_interval;
      executor_options.task_scan_interval = config.executor.task_scan_interval;
      executor_options.suspend_time = config.executor.suspend_time;
    } else {
      // Backward-compatible default for old config files.
      executor_options.core_thread_num = config.thread_count;
      executor_options.max_thread_num = config.thread_count;
    }

    PluginLoader loader;
    auto server = AllocateUnique<bsrvcore::HttpServer>(executor_options);
    ApplyConfigToServer(config, &loader, server.get());

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    if (!server->Start(config.thread_count)) {
      std::cerr << "failed to start server" << std::endl;
      return 3;
    }

    std::cout << "bsrvrun started with config: " << config_path << std::endl;
    while (!g_stop_requested.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    server->Stop();
    server.reset();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "bsrvrun error: " << e.what() << std::endl;
    return 2;
  }
}

}  // namespace bsrvcore::runtime

int main(int argc, char** argv) {
  return bsrvcore::runtime::RunMain(argc, argv);
}
