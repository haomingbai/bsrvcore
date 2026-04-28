/**
 * @file default_client_assembler_builder.h
 * @brief Global default RequestAssembler and StreamBuilder singletons for
 *        backwards-compatible Create* factory paths.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-28
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CONNECTION_CLIENT_IMPL_DEFAULT_CLIENT_ASSEMBLER_BUILDER_H_
#define BSRVCORE_CONNECTION_CLIENT_IMPL_DEFAULT_CLIENT_ASSEMBLER_BUILDER_H_

#include <memory>

#include "bsrvcore/connection/client/request_assembler.h"
#include "bsrvcore/connection/client/stream_builder.h"

namespace bsrvcore {
namespace connection_internal {

inline std::shared_ptr<DefaultRequestAssembler> GetDefaultRequestAssembler() {
  static auto instance = std::make_shared<DefaultRequestAssembler>();
  return instance;
}

inline std::shared_ptr<DirectStreamBuilder> GetDefaultDirectStreamBuilder() {
  static auto instance = DirectStreamBuilder::Create();
  return instance;
}

}  // namespace connection_internal
}  // namespace bsrvcore

#endif  // BSRVCORE_CONNECTION_CLIENT_IMPL_DEFAULT_CLIENT_ASSEMBLER_BUILDER_H_
