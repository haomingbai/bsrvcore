/**
 * @file request_body_processor_detail.h
 * @brief Internal helpers shared by multipart and PUT body processors.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_INTERNAL_CONNECTION_SERVER_REQUEST_BODY_PROCESSOR_DETAIL_H_
#define BSRVCORE_INTERNAL_CONNECTION_SERVER_REQUEST_BODY_PROCESSOR_DETAIL_H_

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/post.hpp>
#include <boost/beast/http/field.hpp>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "bsrvcore/connection/server/http_server_task.h"

namespace bsrvcore::request_body_internal {

inline std::string_view TrimAscii(std::string_view sv) {
  constexpr std::string_view kWhitespace = " \t\r\n";
  const auto first = sv.find_first_not_of(kWhitespace);
  if (first == std::string_view::npos) {
    return {};
  }

  const auto last = sv.find_last_not_of(kWhitespace);
  return sv.substr(first, last - first + 1);
}

inline std::string ToLowerAscii(std::string_view sv) {
  std::string out;
  out.reserve(sv.size());
  for (unsigned char ch : sv) {
    out.push_back(static_cast<char>(std::tolower(ch)));
  }
  return out;
}

inline std::string Unquote(std::string_view sv) {
  sv = TrimAscii(sv);
  if (sv.size() >= 2 && sv.front() == '"' && sv.back() == '"') {
    sv.remove_prefix(1);
    sv.remove_suffix(1);
  }
  return std::string(sv);
}

inline std::optional<std::string> ExtractBoundary(std::string_view content_type) {
  const auto semi = content_type.find(';');
  const auto mime = ToLowerAscii(TrimAscii(content_type.substr(0, semi)));
  if (mime != "multipart/form-data" || semi == std::string_view::npos) {
    return std::nullopt;
  }

  std::string_view params = content_type.substr(semi + 1);
  while (!params.empty()) {
    const auto next = params.find(';');
    auto token = TrimAscii(params.substr(0, next));
    const auto eq = token.find('=');
    if (eq != std::string_view::npos) {
      const auto key = ToLowerAscii(TrimAscii(token.substr(0, eq)));
      if (key == "boundary") {
        auto boundary = Unquote(token.substr(eq + 1));
        if (!boundary.empty()) {
          return boundary;
        }
      }
    }

    if (next == std::string_view::npos) {
      break;
    }
    params.remove_prefix(next + 1);
  }

  return std::nullopt;
}

inline void ParseDisposition(std::string_view value, bool* is_file) {
  if (!is_file) {
    return;
  }

  *is_file = false;
  const auto first_semi = value.find(';');
  if (first_semi == std::string_view::npos) {
    return;
  }

  std::string_view params = value.substr(first_semi + 1);
  while (!params.empty()) {
    const auto next = params.find(';');
    auto token = TrimAscii(params.substr(0, next));
    const auto eq = token.find('=');
    if (eq != std::string_view::npos) {
      const auto key = ToLowerAscii(TrimAscii(token.substr(0, eq)));
      if (key == "filename") {
        *is_file = true;
        return;
      }
    }

    if (next == std::string_view::npos) {
      break;
    }
    params.remove_prefix(next + 1);
  }
}

inline void ParsePartHeaders(std::string_view header_block,
                             std::string* content_type, bool* is_file) {
  if (content_type) {
    content_type->clear();
  }
  if (is_file) {
    *is_file = false;
  }

  while (!header_block.empty()) {
    const auto line_end = header_block.find("\r\n");
    const auto line = header_block.substr(0, line_end);
    const auto colon = line.find(':');
    if (colon != std::string_view::npos) {
      const auto name = ToLowerAscii(TrimAscii(line.substr(0, colon)));
      const auto value = TrimAscii(line.substr(colon + 1));
      if (name == "content-type" && content_type) {
        *content_type = std::string(value);
      } else if (name == "content-disposition") {
        ParseDisposition(value, is_file);
      }
    }

    if (line_end == std::string_view::npos) {
      break;
    }
    header_block.remove_prefix(line_end + 2);
  }
}

inline std::string_view GetHeaderValue(const HttpRequest& request,
                                       boost::beast::http::field field) {
  const auto it = request.find(field);
  if (it == request.end()) {
    return {};
  }
  return it->value();
}

/**
 * @brief Finish async file writes on the caller-selected callback executor.
 *
 * @details
 * Dump helpers are used by both multipart and PUT wrappers. Centralizing the
 * callback hop keeps both code paths consistent while still leaving the caller
 * responsible for choosing the work and callback executors.
 */
inline void DispatchDumpCallback(boost::asio::any_io_executor executor,
                                 std::function<void(bool)> callback, bool ok) {
  if (!callback) {
    return;
  }

  if (executor) {
    boost::asio::post(executor,
                      [callback = std::move(callback), ok]() mutable {
                        callback(ok);
                      });
    return;
  }

  callback(ok);
}

struct AsyncFileWriteState {
  AsyncFileWriteState(boost::asio::any_io_executor callback_executor_in,
                      std::filesystem::path path_in, std::string payload_in,
                      std::function<void(bool)> callback_in)
      : callback_executor(std::move(callback_executor_in)),
        path(std::move(path_in)),
        payload(std::move(payload_in)),
        callback(std::move(callback_in)) {}

  /**
   * @brief Execute the blocking file write on the dedicated work executor.
   *
   * @details
   * Body processors already run after the request body is fully buffered. The
   * only expensive step left is the filesystem write, so this object owns the
   * payload and reports completion through a second explicit executor hop.
   */
  void Run() {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
      Finish(false);
      return;
    }

    if (!payload.empty()) {
      out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    }
    out.flush();
    const bool ok = out.good();
    out.close();
    Finish(ok);
  }

  inline void Finish(bool ok) {
    DispatchDumpCallback(std::move(callback_executor), std::move(callback), ok);
  }

  boost::asio::any_io_executor callback_executor;
  std::filesystem::path path;
  std::string payload;
  std::function<void(bool)> callback;
};

/**
 * @brief Schedule a file dump as two explicit hops.
 *
 * @details
 * The first hop moves blocking disk I/O off the request worker. The second hop
 * posts the completion callback back to the caller-selected executor so route
 * code can keep using its normal server/task threading model.
 */
inline void AsyncDumpPayload(boost::asio::any_io_executor work_executor,
                             boost::asio::any_io_executor callback_executor,
                             std::filesystem::path path, std::string payload,
                             std::function<void(bool)> callback) {
  auto state = std::make_shared<AsyncFileWriteState>(
      std::move(callback_executor), std::move(path), std::move(payload),
      std::move(callback));
  boost::asio::post(std::move(work_executor),
                    [state = std::move(state)]() { state->Run(); });
}

}  // namespace bsrvcore::request_body_internal

#endif
