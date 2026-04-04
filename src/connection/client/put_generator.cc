/**
 * @file put_generator.cc
 * @brief PutGenerator implementation.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-04
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/connection/client/put_generator.h"

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
#include "impl/http_url_parser.h"

namespace bsrvcore {

namespace {

namespace http = boost::beast::http;

using connection_internal::ParseHttpUrl;

inline void DispatchReadyCallback(const HttpClientTask::Executor& executor,
                                  PutGenerator::ReadyCallback callback,
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

struct PutBuildState {
  explicit PutBuildState(HttpClientTask::Executor executor_in)
      : executor(std::move(executor_in)) {}

  HttpClientTask::Executor executor;
  std::string host;
  std::string port;
  std::string target;
  HttpClientOptions options;
  std::string content_type;
  bool use_ssl{false};
  boost::asio::ssl::context* ssl_ctx{nullptr};
  PutGenerator::ReadyCallback callback;
  std::atomic_bool done{false};

  void Complete(std::error_code ec, std::shared_ptr<HttpClientTask> task) {
    if (done.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    DispatchReadyCallback(executor, std::move(callback), ec, std::move(task));
  }

  void BuildTask(std::vector<std::shared_ptr<FileReadingState>> states) {
    if (states.size() != 1 || !states.front()) {
      Complete(std::make_error_code(std::errc::io_error), nullptr);
      return;
    }

    const auto& state = states.front();
    if (state->ec || !state->writer || !state->writer->IsValid()) {
      Complete(
          state->ec ? state->ec : std::make_error_code(std::errc::io_error),
          nullptr);
      return;
    }

    auto task =
        use_ssl ? HttpClientTask::CreateHttps(executor, *ssl_ctx, host, port,
                                              target, http::verb::put, options)
                : HttpClientTask::CreateHttp(executor, host, port, target,
                                             http::verb::put, options);
    if (state->writer->Size() != 0) {
      task->Request().body().assign(
          state->writer->Data(), state->writer->Data() + state->writer->Size());
    } else {
      task->Request().body().clear();
    }
    task->Request().set(
        http::field::content_type,
        content_type.empty() ? "application/octet-stream" : content_type);
    Complete({}, std::move(task));
  }
};

}  // namespace

std::shared_ptr<PutGenerator> PutGenerator::CreateHttp(
    HttpClientTask::Executor executor, std::string host, std::string port,
    std::string target, HttpClientOptions options) {
  struct SharedEnabler final : PutGenerator {
    SharedEnabler(HttpClientTask::Executor executor_in, std::string host_in,
                  std::string port_in, std::string target_in,
                  HttpClientOptions options_in, bool use_ssl_in,
                  boost::asio::ssl::context* ssl_ctx_in)
        : PutGenerator(PrivateTag{}, std::move(executor_in), std::move(host_in),
                       std::move(port_in), std::move(target_in),
                       std::move(options_in), use_ssl_in, ssl_ctx_in) {}
  };

  return AllocateShared<SharedEnabler>(std::move(executor), std::move(host),
                                       std::move(port), std::move(target),
                                       std::move(options), false, nullptr);
}

std::shared_ptr<PutGenerator> PutGenerator::CreateHttps(
    HttpClientTask::Executor executor, boost::asio::ssl::context& ssl_ctx,
    std::string host, std::string port, std::string target,
    HttpClientOptions options) {
  struct SharedEnabler final : PutGenerator {
    SharedEnabler(HttpClientTask::Executor executor_in, std::string host_in,
                  std::string port_in, std::string target_in,
                  HttpClientOptions options_in, bool use_ssl_in,
                  boost::asio::ssl::context* ssl_ctx_in)
        : PutGenerator(PrivateTag{}, std::move(executor_in), std::move(host_in),
                       std::move(port_in), std::move(target_in),
                       std::move(options_in), use_ssl_in, ssl_ctx_in) {}
  };

  return AllocateShared<SharedEnabler>(std::move(executor), std::move(host),
                                       std::move(port), std::move(target),
                                       std::move(options), true, &ssl_ctx);
}

std::shared_ptr<PutGenerator> PutGenerator::CreateFromUrl(
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
    client->create_error_ = std::make_error_code(std::errc::invalid_argument);
  }
  return client;
}

std::shared_ptr<PutGenerator> PutGenerator::CreateFromUrl(
    HttpClientTask::Executor executor, boost::asio::ssl::context& ssl_ctx,
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
  client->ssl_ctx_ = parsed->https ? &ssl_ctx : nullptr;
  return client;
}

PutGenerator::PutGenerator(PrivateTag, HttpClientTask::Executor executor,
                           std::string host, std::string port,
                           std::string target, HttpClientOptions options,
                           bool use_ssl, boost::asio::ssl::context* ssl_ctx)
    : executor_(std::move(executor)),
      host_(std::move(host)),
      port_(std::move(port)),
      target_(std::move(target)),
      options_(std::move(options)),
      use_ssl_(use_ssl),
      ssl_ctx_(ssl_ctx) {}

PutGenerator& PutGenerator::SetFile(std::shared_ptr<FileReader> reader) {
  reader_ = std::move(reader);
  return *this;
}

PutGenerator& PutGenerator::SetContentType(std::string content_type) {
  content_type_ = std::move(content_type);
  return *this;
}

bool PutGenerator::AsyncCreateTask(ReadyCallback callback) const {
  if (!callback) {
    return false;
  }

  if (create_error_) {
    DispatchReadyCallback(executor_, std::move(callback), create_error_,
                          nullptr);
    return true;
  }

  if (!reader_) {
    DispatchReadyCallback(executor_, std::move(callback),
                          std::make_error_code(std::errc::invalid_argument),
                          nullptr);
    return true;
  }

  auto build_state = AllocateShared<PutBuildState>(executor_);
  build_state->host = host_;
  build_state->port = port_;
  build_state->target = target_;
  build_state->options = options_;
  build_state->content_type = content_type_;
  build_state->use_ssl = use_ssl_;
  build_state->ssl_ctx = ssl_ctx_;
  build_state->callback = std::move(callback);

  auto waiter =
      AsyncSameTypeWaiter<std::shared_ptr<FileReadingState>>::Create(1);
  waiter->OnReady(
      [build_state](
          std::vector<std::shared_ptr<FileReadingState>> states) mutable {
        PostOrRun(build_state->executor,
                  [build_state, states = std::move(states)]() mutable {
                    build_state->BuildTask(std::move(states));
                  });
      });
  auto callbacks = waiter->MakeCallbacks();
  if (callbacks.empty() ||
      !reader_->AsyncReadFromDisk(std::move(callbacks[0]))) {
    build_state->Complete(
        std::make_error_code(std::errc::operation_not_supported), nullptr);
  }
  return true;
}

}  // namespace bsrvcore
