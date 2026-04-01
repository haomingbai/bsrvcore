/**
 * @file oai_completion_state.cc
 * @brief Immutable state node implementation for OAI completion facade.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-01
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <memory>
#include <utility>

#include "bsrvcore/oai/completion/oai_completion.h"

namespace bsrvcore::oai::completion {

OaiCompletionState::OaiCompletionState(
    std::shared_ptr<OaiCompletionInfo> info,
    std::shared_ptr<OaiModelInfo> model_info, OaiMessage message,
    OaiRequestLog log, std::shared_ptr<OaiCompletionState> previous)
    : info_(std::move(info)),
      model_info_(std::move(model_info)),
      message_(std::move(message)),
      log_(std::move(log)),
      previous_(std::move(previous)) {}

std::shared_ptr<const OaiCompletionInfo> OaiCompletionState::GetInfo() const {
  return info_;
}

std::shared_ptr<const OaiModelInfo> OaiCompletionState::GetModelInfo() const {
  return model_info_;
}

const OaiMessage& OaiCompletionState::GetMessage() const { return message_; }

const OaiRequestLog& OaiCompletionState::GetLog() const { return log_; }

std::shared_ptr<const OaiCompletionState> OaiCompletionState::GetPreviousState()
    const {
  return previous_;
}

}  // namespace bsrvcore::oai::completion
