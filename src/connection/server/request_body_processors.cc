/**
 * @file request_body_processors.cc
 * @brief MultipartParser and PutProcessor implementations.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-25
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <boost/asio/post.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bsrvcore/connection/server/multipart_parser.h"
#include "bsrvcore/connection/server/put_processor.h"

namespace bsrvcore {

namespace {

std::string_view TrimAscii(std::string_view sv) {
  constexpr std::string_view kWhitespace = " \t\r\n";
  const auto first = sv.find_first_not_of(kWhitespace);
  if (first == std::string_view::npos) {
    return {};
  }

  const auto last = sv.find_last_not_of(kWhitespace);
  return sv.substr(first, last - first + 1);
}

std::string ToLowerAscii(std::string_view sv) {
  std::string out;
  out.reserve(sv.size());
  for (unsigned char ch : sv) {
    out.push_back(static_cast<char>(std::tolower(ch)));
  }
  return out;
}

std::string Unquote(std::string_view sv) {
  sv = TrimAscii(sv);
  if (sv.size() >= 2 && sv.front() == '"' && sv.back() == '"') {
    sv.remove_prefix(1);
    sv.remove_suffix(1);
  }
  return std::string(sv);
}

std::optional<std::string> ExtractBoundary(std::string_view content_type) {
  const auto semi = content_type.find(';');
  const auto mime = ToLowerAscii(TrimAscii(content_type.substr(0, semi)));
  if (mime != "multipart/form-data") {
    return std::nullopt;
  }

  if (semi == std::string_view::npos) {
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

void ParseDisposition(std::string_view value, bool* is_file) {
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

void ParsePartHeaders(std::string_view header_block, std::string* content_type,
                      bool* is_file) {
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

void DispatchCallback(boost::asio::any_io_executor executor,
                      std::function<void(bool)> callback, bool ok) {
  if (!callback) {
    return;
  }

  if (executor) {
    boost::asio::post(executor, [callback = std::move(callback), ok]() mutable {
      callback(ok);
    });
    return;
  }

  callback(ok);
}

std::string_view GetHeaderValue(const HttpRequest& request,
                                boost::beast::http::field field) {
  const auto it = request.find(field);
  if (it == request.end()) {
    return {};
  }
  return it->value();
}

struct AsyncFileWriteState {
  AsyncFileWriteState(boost::asio::any_io_executor callback_executor_in,
                      std::filesystem::path path_in, std::string payload_in,
                      std::function<void(bool)> callback_in)
      : callback_executor(std::move(callback_executor_in)),
        path(std::move(path_in)),
        payload(std::move(payload_in)),
        callback(std::move(callback_in)) {}

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

  void Finish(bool ok) {
    if (!callback) {
      return;
    }

    DispatchCallback(std::move(callback_executor), std::move(callback), ok);
  }

  boost::asio::any_io_executor callback_executor;
  std::filesystem::path path;
  std::string payload;
  std::function<void(bool)> callback;
};

void AsyncDumpPayload(boost::asio::any_io_executor work_executor,
                      boost::asio::any_io_executor callback_executor,
                      std::filesystem::path path, std::string payload,
                      std::function<void(bool)> callback) {
  auto state = std::make_shared<AsyncFileWriteState>(
      std::move(callback_executor), std::move(path), std::move(payload),
      std::move(callback));
  boost::asio::post(std::move(work_executor),
                    [state = std::move(state)]() { state->Run(); });
}

}  // namespace

MultipartParser::MultipartParser(HttpTaskBase& task)
    : MultipartParser(task.GetRequest(), task.GetExecutor()) {}

MultipartParser::MultipartParser(const HttpRequest& request,
                                 boost::asio::any_io_executor executor)
    : work_executor_(executor), callback_executor_(std::move(executor)) {
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

bool MultipartParser::AsyncDumpToDisk(std::size_t part_idx,
                                      std::filesystem::path path,
                                      DumpCallback callback) const {
  if (!work_executor_ || (callback && !callback_executor_) ||
      part_idx >= parts_.size() || !parts_[part_idx].is_file || path.empty()) {
    return false;
  }

  AsyncDumpPayload(work_executor_, callback_executor_, std::move(path),
                   parts_[part_idx].payload, std::move(callback));
  return true;
}

void MultipartParser::Parse(const HttpRequest& request) {
  parts_.clear();

  const auto boundary = ExtractBoundary(
      GetHeaderValue(request, boost::beast::http::field::content_type));
  if (!boundary.has_value()) {
    return;
  }

  const std::string delimiter = "--" + *boundary;
  const std::string boundary_marker = "\r\n" + delimiter;
  const auto& body = request.body();
  std::size_t cursor = 0;

  if (body.compare(0, delimiter.size(), delimiter) != 0) {
    return;
  }

  while (cursor < body.size()) {
    if (body.compare(cursor, delimiter.size(), delimiter) != 0) {
      parts_.clear();
      return;
    }

    cursor += delimiter.size();
    if (cursor + 2 <= body.size() && body.compare(cursor, 2, "--") == 0) {
      return;
    }

    if (cursor + 2 > body.size() || body.compare(cursor, 2, "\r\n") != 0) {
      parts_.clear();
      return;
    }
    cursor += 2;

    const auto header_end = body.find("\r\n\r\n", cursor);
    if (header_end == std::string::npos) {
      parts_.clear();
      return;
    }

    PartData part;
    ParsePartHeaders(std::string_view(body).substr(cursor, header_end - cursor),
                     &part.content_type, &part.is_file);

    const auto payload_start = header_end + 4;
    const auto next_boundary = body.find(boundary_marker, payload_start);
    if (next_boundary == std::string::npos) {
      parts_.clear();
      return;
    }

    part.payload = body.substr(payload_start, next_boundary - payload_start);
    parts_.push_back(std::move(part));
    cursor = next_boundary + 2;
  }
}

PutProcessor::PutProcessor(HttpTaskBase& task)
    : PutProcessor(task.GetRequest(), task.GetExecutor()) {}

PutProcessor::PutProcessor(const HttpRequest& request,
                           boost::asio::any_io_executor executor)
    : body_(request.body()),
      work_executor_(executor),
      callback_executor_(std::move(executor)),
      is_put_(request.method() == boost::beast::http::verb::put) {}

bool PutProcessor::AsyncDumpToDisk(std::filesystem::path path,
                                   DumpCallback callback) const {
  if (!work_executor_ || (callback && !callback_executor_) || !is_put_ ||
      path.empty()) {
    return false;
  }

  AsyncDumpPayload(work_executor_, callback_executor_, std::move(path), body_,
                   std::move(callback));
  return true;
}

}  // namespace bsrvcore
