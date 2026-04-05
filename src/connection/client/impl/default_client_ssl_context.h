/**
 * @file default_client_ssl_context.h
 * @brief Internal helpers for default client TLS contexts.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-05
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CONNECTION_CLIENT_IMPL_DEFAULT_CLIENT_SSL_CONTEXT_H_
#define BSRVCORE_CONNECTION_CLIENT_IMPL_DEFAULT_CLIENT_SSL_CONTEXT_H_

#include <boost/asio/ssl/context.hpp>
#include <boost/system/error_code.hpp>
#include <memory>
#include <string>

#include "bsrvcore/core/types.h"

namespace bsrvcore::connection_internal {

struct DefaultClientSslContextState {
  SslContextPtr ssl_ctx;
  boost::system::error_code ec;
  std::string error_message;
};

const DefaultClientSslContextState& GetDefaultClientSslContextState();

}  // namespace bsrvcore::connection_internal

#endif  // BSRVCORE_CONNECTION_CLIENT_IMPL_DEFAULT_CLIENT_SSL_CONTEXT_H_
