/**
 * @file put_processor.h
 * @brief PUT request body wrapper.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-25
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CONNECTION_SERVER_PUT_PROCESSOR_H_
#define BSRVCORE_CONNECTION_SERVER_PUT_PROCESSOR_H_

#include <boost/asio/any_io_executor.hpp>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>

#include "bsrvcore/connection/server/http_server_task.h"

namespace bsrvcore {

/**
 * @brief Wraps an already-buffered PUT request body for async disk dumping.
 */
class PutProcessor {
 public:
  using DumpCallback = std::function<void(bool)>;

  explicit PutProcessor(HttpTaskBase& task);
  explicit PutProcessor(
      const HttpRequest& request,
      boost::asio::any_io_executor executor = boost::asio::any_io_executor());

  /**
   * @brief Dump the PUT request body to disk asynchronously.
   * @param path Destination path.
   * @param callback Completion callback receiving final write success/failure.
   * @return true if the request body is dumpable and work was scheduled.
   */
  bool AsyncDumpToDisk(std::filesystem::path path, DumpCallback callback) const;

  /**
   * @brief Dump the PUT request body to disk asynchronously and ignore the
   *        final completion result.
   * @param path Destination path.
   * @return true if the request body is dumpable and work was scheduled.
   */
  bool AsyncDumpToDisk(std::filesystem::path path) const {
    return AsyncDumpToDisk(std::move(path), DumpCallback{});
  }

 private:
  std::string body_;
  boost::asio::any_io_executor executor_;
  bool is_put_{false};
};

}  // namespace bsrvcore

#endif
