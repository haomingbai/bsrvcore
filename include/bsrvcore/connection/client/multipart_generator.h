/**
 * @file multipart_generator.h
 * @brief Asynchronous multipart/form-data request builder backed by FileReader.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-04
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CONNECTION_CLIENT_MULTIPART_GENERATOR_H_
#define BSRVCORE_CONNECTION_CLIENT_MULTIPART_GENERATOR_H_

#include <boost/asio/ssl/context.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "bsrvcore/connection/client/http_client_task.h"
#include "bsrvcore/core/trait.h"
#include "bsrvcore/file/file_reader.h"

namespace bsrvcore {

/**
 * @brief Asynchronously load files and build an unstarted multipart/form-data
 * task.
 */
class MultipartGenerator
    : public std::enable_shared_from_this<MultipartGenerator>,
      public NonCopyableNonMovable<MultipartGenerator> {
 public:
  /** @brief Callback invoked when the client task is ready or creation fails.
   */
  using ReadyCallback =
      std::function<void(std::error_code, std::shared_ptr<HttpClientTask>)>;

  /**
   * @brief One multipart section recorded by the generator before body
   * assembly.
   */
  struct PartSpec : public CopyableMovable<PartSpec> {
    /** @brief True when the part consumes a FileReader instead of inline text.
     */
    bool is_file{false};
    /** @brief Index into the internal file-part list. */
    std::size_t file_index{0};
    /** @brief Multipart field name. */
    std::string name;
    /** @brief Inline text value for non-file parts. */
    std::string value;
    /** @brief File source for file parts. */
    std::shared_ptr<FileReader> reader;
    /** @brief Optional filename override for file parts. */
    std::string filename_override;
    /** @brief Optional Content-Type override for this part. */
    std::string content_type_override;
  };

  /** @brief Create an HTTP multipart generator. */
  [[nodiscard]] static std::shared_ptr<MultipartGenerator> CreateHttp(
      HttpClientTask::Executor executor, std::string host, std::string port,
      std::string target, HttpClientOptions options = {});
  /** @brief Create an HTTPS multipart generator. */
  [[nodiscard]] static std::shared_ptr<MultipartGenerator> CreateHttps(
      HttpClientTask::Executor executor, boost::asio::ssl::context& ssl_ctx,
      std::string host, std::string port, std::string target,
      HttpClientOptions options = {});
  /** @brief Create a multipart generator from a URL without an SSL context. */
  [[nodiscard]] static std::shared_ptr<MultipartGenerator> CreateFromUrl(
      HttpClientTask::Executor executor, const std::string& url,
      HttpClientOptions options = {});
  /** @brief Create a multipart generator from a URL with an SSL context. */
  [[nodiscard]] static std::shared_ptr<MultipartGenerator> CreateFromUrl(
      HttpClientTask::Executor executor, boost::asio::ssl::context& ssl_ctx,
      const std::string& url, HttpClientOptions options = {});

  /** @brief Add one file-backed multipart section. */
  MultipartGenerator& AddFilePart(std::string name,
                                  std::shared_ptr<FileReader> reader,
                                  std::string filename_override = {},
                                  std::string content_type_override = {});
  /** @brief Add one inline text multipart section. */
  MultipartGenerator& AddTextPart(
      std::string name, std::string value,
      std::string content_type = "text/plain; charset=utf-8");

  /** @brief Read required files and create an unstarted HttpClientTask. */
  bool AsyncCreateTask(ReadyCallback callback) const;

 private:
  struct PrivateTag {};

  MultipartGenerator(PrivateTag, HttpClientTask::Executor executor,
                     std::string host, std::string port, std::string target,
                     HttpClientOptions options, bool use_ssl,
                     boost::asio::ssl::context* ssl_ctx);

  HttpClientTask::Executor executor_;
  std::string host_;
  std::string port_;
  std::string target_;
  HttpClientOptions options_;
  std::vector<PartSpec> parts_;
  bool use_ssl_{false};
  boost::asio::ssl::context* ssl_ctx_{nullptr};
  std::error_code create_error_;
  std::size_t file_part_count_{0};
};

}  // namespace bsrvcore

#endif  // BSRVCORE_CONNECTION_CLIENT_MULTIPART_GENERATOR_H_
