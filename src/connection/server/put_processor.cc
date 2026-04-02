/**
 * @file put_processor.cc
 * @brief PutProcessor implementation.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/connection/server/put_processor.h"

#include <boost/asio/any_io_executor.hpp>
#include <boost/beast/http/verb.hpp>
#include <filesystem>
#include <utility>

#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/internal/connection/server/request_body_processor_detail.h"

namespace bsrvcore {

PutProcessor::PutProcessor(HttpTaskBase& task)
    : PutProcessor(task.GetRequest(), task.GetExecutor()) {}

PutProcessor::PutProcessor(const HttpRequest& request,
                           boost::asio::any_io_executor executor)
    : body_(request.body()),
      work_executor_(executor),
      callback_executor_(std::move(executor)),
      is_put_(request.method() == boost::beast::http::verb::put) {}

bool PutProcessor::AsyncDumpToDisk(std::filesystem::path path,
                                   DumpCallback callback) const {
  if (!work_executor_ || (callback && !callback_executor_) || !is_put_ ||
      path.empty()) {
    return false;
  }

  request_body_internal::AsyncDumpPayload(work_executor_, callback_executor_,
                                          std::move(path), body_,
                                          std::move(callback));
  return true;
}

}  // namespace bsrvcore
