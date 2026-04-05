/**
 * @file multipart_parser.cc
 * @brief MultipartParser implementation.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/connection/server/multipart_parser.h"

#include <boost/asio/any_io_executor.hpp>
#include <boost/beast/http/field.hpp>
#include <cstddef>
#include <cstdint>  // NOLINT(misc-include-cleaner): Boost.Beast field.hpp requires std::uint32_t on some toolchains.
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/file/file_state.h"
#include "internal/server/request_body_processor_detail.h"

namespace bsrvcore {

namespace {

using request_body_internal::ExtractBoundary;
using request_body_internal::GetHeaderValue;
using request_body_internal::ParsePartHeaders;

inline bool PrepareMultipartBody(std::string_view body,
                                 const std::string& delimiter,
                                 std::size_t* cursor) {
  if ((cursor == nullptr) || !body.starts_with(delimiter)) {
    return false;
  }

  *cursor = 0;
  return true;
}

inline bool ParseMultipartPart(std::string_view body,
                               const std::string& delimiter,
                               const std::string& boundary_marker,
                               std::size_t* cursor, std::string* content_type,
                               std::string* payload, bool* is_file) {
  if ((cursor == nullptr) || (content_type == nullptr) ||
      (payload == nullptr) || (is_file == nullptr) ||
      body.compare(*cursor, delimiter.size(), delimiter) != 0) {
    return false;
  }

  *cursor += delimiter.size();
  if (*cursor + 2 <= body.size() && body.compare(*cursor, 2, "--") == 0) {
    return false;
  }

  if (*cursor + 2 > body.size() || body.compare(*cursor, 2, "\r\n") != 0) {
    return false;
  }
  *cursor += 2;

  const auto header_end = body.find("\r\n\r\n", *cursor);
  if (header_end == std::string::npos) {
    return false;
  }

  ParsePartHeaders(body.substr(*cursor, header_end - *cursor), content_type,
                   is_file);

  const auto payload_start = header_end + 4;
  const auto next_boundary = body.find(boundary_marker, payload_start);
  if (next_boundary == std::string::npos) {
    return false;
  }

  *payload =
      std::string(body.substr(payload_start, next_boundary - payload_start));
  *cursor = next_boundary + 2;
  return true;
}

inline bool ReachedMultipartTerminator(std::string_view body,
                                       std::size_t cursor,
                                       const std::string& delimiter) {
  return body.compare(cursor, delimiter.size(), delimiter) == 0 &&
         cursor + delimiter.size() + 2 <= body.size() &&
         body.compare(cursor + delimiter.size(), 2, "--") == 0;
}

}  // namespace

std::shared_ptr<MultipartParser> MultipartParser::Create(HttpTaskBase& task) {
  return Create(task.GetRequest(), task.GetExecutor(), task.GetExecutor());
}

std::shared_ptr<MultipartParser> MultipartParser::Create(
    const HttpRequest& request, IoExecutor work_executor,
    IoExecutor callback_executor) {
  struct SharedEnabler final : MultipartParser {
    SharedEnabler(const HttpRequest& request_in, IoExecutor work_executor_in,
                  IoExecutor callback_executor_in)
        : MultipartParser(PrivateTag{}, request_in, std::move(work_executor_in),
                          std::move(callback_executor_in)) {}
  };

  return AllocateShared<SharedEnabler>(request, std::move(work_executor),
                                       std::move(callback_executor));
}

std::shared_ptr<MultipartParser> MultipartParser::Create(
    const HttpRequest& request, IoExecutor executor) {
  return Create(request, executor, executor);
}

MultipartParser::MultipartParser(PrivateTag, const HttpRequest& request,
                                 IoExecutor work_executor,
                                 IoExecutor callback_executor)
    : work_executor_(std::move(work_executor)),
      callback_executor_(std::move(callback_executor)) {
  Parse(request);
}

std::size_t MultipartParser::GetPartCount() const noexcept {
  return parts_.size();
}

std::string_view MultipartParser::GetPartType(
    std::size_t part_idx) const noexcept {
  if (part_idx >= parts_.size()) {
    return {};
  }
  return parts_[part_idx].content_type;
}

bool MultipartParser::IsFile(std::size_t part_idx) const noexcept {
  if (part_idx >= parts_.size()) {
    return false;
  }
  return parts_[part_idx].is_file;
}

std::shared_ptr<FileWriter> MultipartParser::GetFileWriter(
    std::size_t part_idx) const noexcept {
  if (part_idx >= parts_.size()) {
    return nullptr;
  }
  return parts_[part_idx].writer;
}

bool MultipartParser::AsyncDumpToDisk(std::size_t part_idx,
                                      std::filesystem::path path,
                                      DumpCallback callback) const {
  auto writer = GetFileWriter(part_idx);
  if (!writer || path.empty()) {
    return false;
  }

  if (!callback) {
    return writer->AsyncWriteToDisk(std::move(path));
  }

  return writer->AsyncWriteToDisk(
      std::move(path), FileWritingState::Create(),
      [callback = std::move(callback)](
          const std::shared_ptr<FileWritingState>& state) mutable {
        callback(state && !state->ec);
      });
}

void MultipartParser::Parse(const HttpRequest& request) {
  parts_.clear();

  const auto boundary =
      ExtractBoundary(GetHeaderValue(request, HttpField::content_type));
  if (!boundary.has_value()) {
    return;
  }

  const std::string delimiter = "--" + *boundary;
  const std::string boundary_marker = "\r\n" + delimiter;
  const std::string_view body = request.body();
  std::size_t cursor = 0;
  if (!PrepareMultipartBody(body, delimiter, &cursor)) {
    return;
  }

  while (cursor < body.size()) {
    if (ReachedMultipartTerminator(body, cursor, delimiter)) {
      return;
    }

    PartData part;
    if (!ParseMultipartPart(body, delimiter, boundary_marker, &cursor,
                            &part.content_type, &part.payload, &part.is_file)) {
      parts_.clear();
      return;
    }
    if (part.is_file) {
      part.writer =
          FileWriter::Create(part.payload, work_executor_, callback_executor_);
      part.payload.clear();
    }
    parts_.push_back(std::move(part));
  }
}

}  // namespace bsrvcore
