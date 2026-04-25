/**
 * @file default_client_ssl_context.cc
 * @brief Internal helpers for default client TLS contexts.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-05
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "default_client_ssl_context.h"

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <array>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/system/errc.hpp>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "bsrvcore/allocator/allocator.h"

namespace bsrvcore::connection_internal {

namespace {

namespace fs = std::filesystem;
using VerifyPathList = AllocatedVector<AllocatedString>;

boost::system::error_code MakeSslError() {
  const auto ssl_error = ::ERR_get_error();
  if (ssl_error == 0) {
    return {};
  }

  return {static_cast<int>(ssl_error), boost::asio::error::get_ssl_category()};
}

bool PathExists(std::string_view path) {
  if (path.empty()) {
    return false;
  }

  std::error_code ec;
  return fs::exists(fs::path(path), ec) && !ec;
}

bool TryLoadVerifyFile(SslContext& ssl_ctx, std::string_view path,
                       boost::system::error_code& first_error) {
  if (!PathExists(path)) {
    return false;
  }

  if (::SSL_CTX_load_verify_file(ssl_ctx.native_handle(),
                                 std::string(path).c_str()) == 1) {
    return true;
  }

  if (!first_error) {
    first_error = MakeSslError();
    if (!first_error) {
      first_error = make_error_code(boost::system::errc::io_error);
    }
  }
  return false;
}

bool TryLoadVerifyDir(SslContext& ssl_ctx, std::string_view path,
                      boost::system::error_code& first_error) {
  if (!PathExists(path)) {
    return false;
  }

  if (::SSL_CTX_load_verify_dir(ssl_ctx.native_handle(),
                                std::string(path).c_str()) == 1) {
    return true;
  }

  if (!first_error) {
    first_error = MakeSslError();
    if (!first_error) {
      first_error = make_error_code(boost::system::errc::io_error);
    }
  }
  return false;
}

VerifyPathList CollectVerifyFiles() {
  VerifyPathList files;
  if (const char* ssl_cert_file = std::getenv("SSL_CERT_FILE");
      ssl_cert_file != nullptr && *ssl_cert_file != '\0') {
    files.emplace_back(ssl_cert_file);
  }

  for (std::string_view path : std::array<std::string_view, 5>{
           "/etc/ssl/cert.pem", "/etc/ssl/certs/ca-certificates.crt",
           "/etc/ssl/ca-bundle.pem", "/etc/pki/tls/cert.pem",
           "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem"}) {
    files.emplace_back(path);
  }

  return files;
}

VerifyPathList CollectVerifyDirs() {
  VerifyPathList dirs;
  if (const char* ssl_cert_dir = std::getenv("SSL_CERT_DIR");
      ssl_cert_dir != nullptr && *ssl_cert_dir != '\0') {
    dirs.emplace_back(ssl_cert_dir);
  }

  for (std::string_view path :
       std::array<std::string_view, 3>{"/etc/ssl/certs", "/etc/pki/tls/certs",
                                       "/system/etc/security/cacerts"}) {
    dirs.emplace_back(path);
  }

  return dirs;
}

DefaultClientSslContextState BuildDefaultClientSslContextState() {
  DefaultClientSslContextState state;
  auto ssl_ctx = std::make_shared<SslContext>(SslContext::tls_client);

  bool loaded_any = false;
  boost::system::error_code first_error;

  boost::system::error_code default_paths_ec;
  ssl_ctx->set_default_verify_paths(default_paths_ec);
  if (!default_paths_ec) {
    loaded_any = true;
  } else {
    first_error = default_paths_ec;
  }

  for (const auto& file : CollectVerifyFiles()) {
    loaded_any = TryLoadVerifyFile(*ssl_ctx, file, first_error) || loaded_any;
  }
  for (const auto& dir : CollectVerifyDirs()) {
    loaded_any = TryLoadVerifyDir(*ssl_ctx, dir, first_error) || loaded_any;
  }

  if (!loaded_any) {
    state.ec =
        first_error
            ? first_error
            : make_error_code(boost::system::errc::no_such_file_or_directory);
    state.error_message = "failed to load trusted CA certificates";
    return state;
  }

  state.ssl_ctx = std::move(ssl_ctx);
  return state;
}

}  // namespace

const DefaultClientSslContextState& GetDefaultClientSslContextState() {
  static const DefaultClientSslContextState state =
      BuildDefaultClientSslContextState();
  return state;
}

}  // namespace bsrvcore::connection_internal
