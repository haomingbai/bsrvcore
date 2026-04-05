/**
 * @file put_generator.h
 * @brief Asynchronous PUT request builder backed by FileReader.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-04
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CONNECTION_CLIENT_PUT_GENERATOR_H_
#define BSRVCORE_CONNECTION_CLIENT_PUT_GENERATOR_H_

#include <boost/asio/ssl/context.hpp>
#include <functional>
#include <memory>
#include <string>

#include "bsrvcore/connection/client/http_client_task.h"
#include "bsrvcore/core/trait.h"
#include "bsrvcore/file/file_reader.h"

namespace bsrvcore {

/**
 * @brief Asynchronously load one file and turn it into an unstarted PUT task.
 */
class PutGenerator : public std::enable_shared_from_this<PutGenerator>,
                     public NonCopyableNonMovable<PutGenerator> {
 public:
  /** @brief Callback invoked when the client task is ready or creation fails.
   */
  using ReadyCallback =
      std::function<void(std::error_code, std::shared_ptr<HttpClientTask>)>;

  /** @brief Create an HTTP PUT generator. */
  [[nodiscard]] static std::shared_ptr<PutGenerator> CreateHttp(
      HttpClientTask::Executor executor, std::string host, std::string port,
      std::string target, HttpClientOptions options = {});
  /** @brief Create an HTTPS PUT generator. */
  [[nodiscard]] static std::shared_ptr<PutGenerator> CreateHttps(
      HttpClientTask::Executor executor, std::string host, std::string port,
      std::string target, HttpClientOptions options = {});
  /** @brief Create an HTTPS PUT generator with caller-provided TLS context. */
  [[nodiscard]] static std::shared_ptr<PutGenerator> CreateHttps(
      HttpClientTask::Executor executor,
      std::shared_ptr<boost::asio::ssl::context> ssl_ctx, std::string host,
      std::string port, std::string target, HttpClientOptions options = {});
  /** @brief Create a PUT generator from a URL without an SSL context. */
  [[nodiscard]] static std::shared_ptr<PutGenerator> CreateFromUrl(
      HttpClientTask::Executor executor, const std::string& url,
      HttpClientOptions options = {});
  /** @brief Create a PUT generator from a URL with an SSL context. */
  [[nodiscard]] static std::shared_ptr<PutGenerator> CreateFromUrl(
      HttpClientTask::Executor executor,
      std::shared_ptr<boost::asio::ssl::context> ssl_ctx,
      const std::string& url, HttpClientOptions options = {});

  /** @brief Set the file that will become the PUT request body. */
  PutGenerator& SetFile(std::shared_ptr<FileReader> reader);
  /** @brief Override the outgoing request Content-Type header. */
  PutGenerator& SetContentType(std::string content_type);

  /** @brief Read the file and create an unstarted HttpClientTask. */
  bool AsyncCreateTask(ReadyCallback callback) const;

 private:
  struct PrivateTag {};

  PutGenerator(PrivateTag, HttpClientTask::Executor executor, std::string host,
               std::string port, std::string target, HttpClientOptions options,
               bool use_ssl,
               std::shared_ptr<boost::asio::ssl::context> ssl_ctx);

  HttpClientTask::Executor executor_;
  std::string host_;
  std::string port_;
  std::string target_;
  HttpClientOptions options_;
  std::shared_ptr<FileReader> reader_;
  std::string content_type_;
  bool use_ssl_{false};
  std::shared_ptr<boost::asio::ssl::context> ssl_ctx_;
  std::error_code create_error_;
};

}  // namespace bsrvcore

#endif  // BSRVCORE_CONNECTION_CLIENT_PUT_GENERATOR_H_
