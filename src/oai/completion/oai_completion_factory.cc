/**
 * @file oai_completion_factory.cc
 * @brief Factory basics for OAI completion facade.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-01
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <chrono>
#include <string>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/oai/completion/oai_completion.h"

namespace bsrvcore::oai::completion {

namespace {

std::int64_t NowUnixMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

OaiCompletionFactory::OaiCompletionFactory(
    boost::asio::io_context::executor_type executor,
    std::shared_ptr<OaiCompletionInfo> info)
    : executor_(std::move(executor)),
      info_(std::move(info)),
      ssl_ctx_(AllocateShared<boost::asio::ssl::context>(
          boost::asio::ssl::context::tls_client)) {
  ssl_ctx_->set_default_verify_paths();
}

OaiCompletionFactory::StatePtr OaiCompletionFactory::AppendMessage(
    const OaiMessage& msg, StatePtr prev) const {
  OaiRequestLog log;
  log.status = OaiCompletionStatus::kLocal;
  log.timestamp = NowUnixMs();
  return AllocateShared<OaiCompletionState>(
      info_, std::shared_ptr<OaiModelInfo>{}, msg, std::move(log),
      std::move(prev));
}

}  // namespace bsrvcore::oai::completion
