/**
 * @file multipart_generator.cc
 * @brief MultipartGenerator implementation.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-04
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/connection/client/multipart_generator.h"

#include <atomic>
#include <boost/asio/post.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/system/errc.hpp>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/core/async_waiter.h"
#include "bsrvcore/file/file_writer.h"
#include "impl/default_client_ssl_context.h"
#include "impl/http_url_parser.h"

namespace bsrvcore {

namespace {

namespace http = boost::beast::http;

using connection_internal::ParseHttpUrl;

std::atomic<std::size_t> g_multipart_boundary_counter{0};

inline void DispatchReadyCallback(const HttpClientTask::Executor& executor,
                                  MultipartGenerator::ReadyCallback callback,
                                  std::error_code ec,
                                  std::shared_ptr<HttpClientTask> task) {
  if (!callback) {
    return;
  }

  boost::asio::post(executor, [callback = std::move(callback), ec,
                               task = std::move(task)]() mutable {
    callback(ec, std::move(task));
  });
}

template <typename Fn>
inline void PostOrRun(const HttpClientTask::Executor& executor, Fn&& fn) {
  boost::asio::post(executor, std::forward<Fn>(fn));
}

std::error_code MakeDefaultSslContextError(
    const connection_internal::DefaultClientSslContextState& state) {
  if (!state.ec) {
    return {};
  }
  return std::make_error_code(std::errc::io_error);
}

inline std::string EscapeQuotedParameter(std::string value) {
  std::string out;
  out.reserve(value.size());
  for (char ch : value) {
    if ((ch == '"') || (ch == '\\')) {
      out.push_back('\\');
    }
    out.push_back(ch);
  }
  return out;
}

inline std::string BuildBoundary() {
  return "bsrvcore-boundary-" +
         std::to_string(g_multipart_boundary_counter.fetch_add(
             1, std::memory_order_relaxed));
}

inline std::string DefaultFilename(const std::shared_ptr<FileReader>& reader) {
  if (!reader) {
    return "file";
  }

  const auto filename = reader->GetPath().filename().string();
  return filename.empty() ? "file" : filename;
}

struct MultipartBuildState {
  using PartSpec = MultipartGenerator::PartSpec;

  explicit MultipartBuildState(HttpClientTask::Executor executor_in)
      : executor(std::move(executor_in)) {}

  HttpClientTask::Executor executor;
  std::string host;
  std::string port;
  std::string target;
  HttpClientOptions options;
  std::vector<PartSpec> parts;
  bool use_ssl{false};
  SslContextPtr ssl_ctx;
  MultipartGenerator::ReadyCallback callback;
  std::atomic_bool done{false};

  void Complete(std::error_code ec, std::shared_ptr<HttpClientTask> task) {
    if (done.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    DispatchReadyCallback(executor, std::move(callback), ec, std::move(task));
  }

  void BuildTask(std::vector<std::shared_ptr<FileReadingState>> states) {
    std::string boundary = BuildBoundary();
    std::string body;

    for (const auto& part : parts) {
      body.append("--");
      body.append(boundary);
      body.append("\r\n");

      body.append("Content-Disposition: form-data; name=\"");
      body.append(EscapeQuotedParameter(part.name));
      body.push_back('"');

      if (part.is_file) {
        if (part.file_index >= states.size() || !states[part.file_index] ||
            states[part.file_index]->ec || !states[part.file_index]->writer) {
          Complete(part.file_index < states.size() && states[part.file_index] &&
                           states[part.file_index]->ec
                       ? states[part.file_index]->ec
                       : std::make_error_code(std::errc::io_error),
                   nullptr);
          return;
        }

        const auto& state = states[part.file_index];
        const auto filename = part.filename_override.empty()
                                  ? DefaultFilename(part.reader)
                                  : part.filename_override;
        const auto content_type = part.content_type_override.empty()
                                      ? std::string("application/octet-stream")
                                      : part.content_type_override;

        body.append("; filename=\"");
        body.append(EscapeQuotedParameter(filename));
        body.append("\"\r\n");
        body.append("Content-Type: ");
        body.append(content_type);
        body.append("\r\n\r\n");
        if (state->writer->Size() != 0) {
          body.append(state->writer->Data(), state->writer->Size());
        }
        body.append("\r\n");
        continue;
      }

      body.append("\"\r\n");
      if (!part.content_type_override.empty()) {
        body.append("Content-Type: ");
        body.append(part.content_type_override);
        body.append("\r\n");
      }
      body.append("\r\n");
      body.append(part.value);
      body.append("\r\n");
    }

    body.append("--");
    body.append(boundary);
    body.append("--\r\n");

    auto task =
        use_ssl ? HttpClientTask::CreateHttps(executor, ssl_ctx, host, port,
                                              target, http::verb::post, options)
                : HttpClientTask::CreateHttp(executor, host, port, target,
                                             http::verb::post, options);
    task->Request().body() = std::move(body);
    task->Request().set(http::field::content_type,
                        "multipart/form-data; boundary=" + boundary);
    Complete({}, std::move(task));
  }
};

}  // namespace

std::shared_ptr<MultipartGenerator> MultipartGenerator::CreateHttp(
    HttpClientTask::Executor executor, std::string host, std::string port,
    std::string target, HttpClientOptions options) {
  struct SharedEnabler final : MultipartGenerator {
    SharedEnabler(HttpClientTask::Executor executor_in, std::string host_in,
                  std::string port_in, std::string target_in,
                  HttpClientOptions options_in, bool use_ssl_in,
                  SslContextPtr ssl_ctx_in)
        : MultipartGenerator(PrivateTag{}, std::move(executor_in),
                             std::move(host_in), std::move(port_in),
                             std::move(target_in), std::move(options_in),
                             use_ssl_in, std::move(ssl_ctx_in)) {}
  };

  return AllocateShared<SharedEnabler>(std::move(executor), std::move(host),
                                       std::move(port), std::move(target),
                                       std::move(options), false, nullptr);
}

std::shared_ptr<MultipartGenerator> MultipartGenerator::CreateHttps(
    HttpClientTask::Executor executor, std::string host, std::string port,
    std::string target, HttpClientOptions options) {
  const auto& ssl_state =
      connection_internal::GetDefaultClientSslContextState();
  auto generator =
      CreateHttps(std::move(executor), ssl_state.ssl_ctx, std::move(host),
                  std::move(port), std::move(target), std::move(options));
  if (ssl_state.ec) {
    generator->create_error_ = MakeDefaultSslContextError(ssl_state);
  }
  return generator;
}

std::shared_ptr<MultipartGenerator> MultipartGenerator::CreateHttps(
    HttpClientTask::Executor executor, SslContextPtr ssl_ctx, std::string host,
    std::string port, std::string target, HttpClientOptions options) {
  struct SharedEnabler final : MultipartGenerator {
    SharedEnabler(HttpClientTask::Executor executor_in, std::string host_in,
                  std::string port_in, std::string target_in,
                  HttpClientOptions options_in, bool use_ssl_in,
                  SslContextPtr ssl_ctx_in)
        : MultipartGenerator(PrivateTag{}, std::move(executor_in),
                             std::move(host_in), std::move(port_in),
                             std::move(target_in), std::move(options_in),
                             use_ssl_in, std::move(ssl_ctx_in)) {}
  };

  auto generator = AllocateShared<SharedEnabler>(
      std::move(executor), std::move(host), std::move(port), std::move(target),
      std::move(options), true, std::move(ssl_ctx));
  if (generator->ssl_ctx_ == nullptr) {
    generator->create_error_ =
        std::make_error_code(std::errc::invalid_argument);
  }
  return generator;
}

std::shared_ptr<MultipartGenerator> MultipartGenerator::CreateFromUrl(
    HttpClientTask::Executor executor, const std::string& url,
    HttpClientOptions options) {
  auto client =
      CreateHttp(std::move(executor), "", "", "/", std::move(options));
  auto parsed = ParseHttpUrl(url);
  if (!parsed) {
    client->create_error_ = std::make_error_code(std::errc::invalid_argument);
    return client;
  }

  client->host_ = parsed->host;
  client->port_ = parsed->port;
  client->target_ = parsed->target;
  client->use_ssl_ = parsed->https;
  if (parsed->https) {
    const auto& ssl_state =
        connection_internal::GetDefaultClientSslContextState();
    client->ssl_ctx_ = ssl_state.ssl_ctx;
    if (ssl_state.ec) {
      client->create_error_ = MakeDefaultSslContextError(ssl_state);
    }
  }
  return client;
}

std::shared_ptr<MultipartGenerator> MultipartGenerator::CreateFromUrl(
    HttpClientTask::Executor executor, SslContextPtr ssl_ctx,
    const std::string& url, HttpClientOptions options) {
  auto client =
      CreateHttp(std::move(executor), "", "", "/", std::move(options));
  auto parsed = ParseHttpUrl(url);
  if (!parsed) {
    client->create_error_ = std::make_error_code(std::errc::invalid_argument);
    return client;
  }

  client->host_ = parsed->host;
  client->port_ = parsed->port;
  client->target_ = parsed->target;
  client->use_ssl_ = parsed->https;
  client->ssl_ctx_ = parsed->https ? std::move(ssl_ctx) : SslContextPtr{};
  if (parsed->https && client->ssl_ctx_ == nullptr) {
    client->create_error_ = std::make_error_code(std::errc::invalid_argument);
  }
  return client;
}

MultipartGenerator::MultipartGenerator(PrivateTag,
                                       HttpClientTask::Executor executor,
                                       std::string host, std::string port,
                                       std::string target,
                                       HttpClientOptions options, bool use_ssl,
                                       SslContextPtr ssl_ctx)
    : executor_(std::move(executor)),
      host_(std::move(host)),
      port_(std::move(port)),
      target_(std::move(target)),
      options_(std::move(options)),
      use_ssl_(use_ssl),
      ssl_ctx_(std::move(ssl_ctx)) {}

MultipartGenerator& MultipartGenerator::AddFilePart(
    std::string name, std::shared_ptr<FileReader> reader,
    std::string filename_override, std::string content_type_override) {
  PartSpec part;
  part.is_file = true;
  part.file_index = file_part_count_++;
  part.name = std::move(name);
  part.reader = std::move(reader);
  part.filename_override = std::move(filename_override);
  part.content_type_override = std::move(content_type_override);
  parts_.push_back(std::move(part));
  return *this;
}

MultipartGenerator& MultipartGenerator::AddTextPart(std::string name,
                                                    std::string value,
                                                    std::string content_type) {
  PartSpec part;
  part.is_file = false;
  part.name = std::move(name);
  part.value = std::move(value);
  part.content_type_override = std::move(content_type);
  parts_.push_back(std::move(part));
  return *this;
}

bool MultipartGenerator::AsyncCreateTask(ReadyCallback callback) const {
  if (!callback) {
    return false;
  }

  if (create_error_) {
    DispatchReadyCallback(executor_, std::move(callback), create_error_,
                          nullptr);
    return true;
  }

  auto build_state = AllocateShared<MultipartBuildState>(executor_);
  build_state->host = host_;
  build_state->port = port_;
  build_state->target = target_;
  build_state->options = options_;
  build_state->parts = parts_;
  build_state->use_ssl = use_ssl_;
  build_state->ssl_ctx = ssl_ctx_;
  build_state->callback = std::move(callback);

  if (file_part_count_ == 0) {
    PostOrRun(build_state->executor,
              [build_state]() mutable { build_state->BuildTask({}); });
    return true;
  }

  auto waiter = AsyncSameTypeWaiter<std::shared_ptr<FileReadingState>>::Create(
      file_part_count_);
  waiter->OnReady(
      [build_state](
          std::vector<std::shared_ptr<FileReadingState>> states) mutable {
        PostOrRun(build_state->executor,
                  [build_state, states = std::move(states)]() mutable {
                    build_state->BuildTask(std::move(states));
                  });
      });

  auto callbacks = waiter->MakeCallbacks();
  std::size_t callback_index = 0;
  for (const auto& part : parts_) {
    if (!part.is_file) {
      continue;
    }
    if (!part.reader) {
      build_state->Complete(std::make_error_code(std::errc::invalid_argument),
                            nullptr);
      return true;
    }
    if (!part.reader->AsyncReadFromDisk(callbacks[callback_index++])) {
      build_state->Complete(
          std::make_error_code(std::errc::operation_not_supported), nullptr);
      return true;
    }
  }

  return true;
}

}  // namespace bsrvcore
