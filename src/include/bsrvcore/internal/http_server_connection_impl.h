/**
 * @file http_server_connection_impl.h
 * @brief Template implementation for HTTP server connections with multiple
 * stream types
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-01
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Template-based implementation of HTTP server connections supporting
 * both plain TCP and SSL streams. Provides efficient message queuing
 * and asynchronous I/O operations for high-performance HTTP serving.
 */

#pragma once

#include <boost/beast/http/field.hpp>
#ifndef BSRVCORE_INTERNAL_HTTP_SERVER_CONNECTION_IMPL_H_
#define BSRVCORE_INTERNAL_HTTP_SERVER_CONNECTION_IMPL_H_

#include <atomic>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/serializer.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/system/detail/error_code.hpp>
#include <cassert>
#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <variant>

#include "bsrvcore/http_server_task.h"
#include "bsrvcore/internal/http_server_connection.h"

namespace bsrvcore {

namespace connection_internal {

namespace helper {

/**
 * @brief Type trait to detect Boost.Beast SSL streams
 * @tparam T Type to check
 */
template <typename T>
struct IsBeastSslStream : std::false_type {};

/**
 * @brief Specialization for Boost.Beast SSL streams
 * @tparam NextLayer Underlying stream type
 */
template <typename NextLayer>
struct IsBeastSslStream<boost::beast::ssl_stream<NextLayer>> : std::true_type {
};

/**
 * @brief Get the underlying TCP socket from a Beast stream
 * @param s TCP stream
 * @return Reference to underlying TCP socket
 */
inline boost::asio::ip::tcp::socket& GetLowestSocket(
    boost::beast::tcp_stream& s) {
  return s.socket();
}

/**
 * @brief Get the underlying TCP socket from a Beast SSL stream
 * @param s SSL stream
 * @return Reference to underlying TCP socket
 */
inline boost::asio::ip::tcp::socket& GetLowestSocket(
    boost::beast::ssl_stream<boost::beast::tcp_stream>& s) {
  return s.next_layer().socket();
}

}  // namespace helper

/**
 * @brief Concept for valid stream types supported by the connection
 * implementation
 * @tparam S Stream type
 *
 * A stream is valid if it provides a GetLowestSocket function that returns
 * a reference to a boost::asio::ip::tcp::socket.
 */
template <typename S>
concept ValidStream = requires(S s) {
  { helper::GetLowestSocket(s) };
};

/**
 * @brief Template implementation of HTTP server connection for various stream
 * types
 *
 * This template class provides concrete implementation of HTTP server
 * connections for different stream types (TCP, SSL, etc.). It handles the
 * transport-specific details while providing a consistent interface for HTTP
 * protocol handling.
 *
 * Features:
 * - Supports both plain TCP and SSL streams
 * - Efficient message queuing for response streaming
 * - Asynchronous I/O with proper error handling
 * - Graceful connection shutdown
 *
 * @tparam S Stream type (must satisfy ValidStream concept)
 *
 * @code
 * // Example usage with TCP stream
 * boost::beast::tcp_stream stream(std::move(socket));
 * auto tcp_conn =
 * std::make_shared<HttpServerConnectionImpl<boost::beast::tcp_stream>>(
 *     std::move(stream), strand, server, 30000, 15000);
 *
 * // Example usage with SSL stream
 * boost::beast::ssl_stream<boost::beast::tcp_stream>
 * ssl_stream(std::move(tcp_stream), ssl_ctx); auto ssl_conn =
 * std::make_shared<HttpServerConnectionImpl<
 *     boost::beast::ssl_stream<boost::beast::tcp_stream>>>(
 *     std::move(ssl_stream), strand, server, 30000, 15000);
 * @endcode
 */
template <ValidStream S>
class HttpServerConnectionImpl : public HttpServerConnection {
 public:
  /**
   * @brief Construct a connection implementation with the specified stream
   * @param stream Boost.Beast stream (TCP or SSL)
   * @param strand ASIO strand for thread safety
   * @param srv HTTP server instance
   * @param header_read_expiry Header read timeout in milliseconds
   * @param keep_alive_timeout Keep-alive timeout in milliseconds
   */
  HttpServerConnectionImpl(
      S stream, boost::asio::strand<boost::asio::any_io_executor> strand,
      std::shared_ptr<HttpServer> srv, std::size_t header_read_expiry,
      std::size_t keep_alive_timeout)
      : HttpServerConnection(std::move(strand), std::move(srv),
                             header_read_expiry, keep_alive_timeout),
        stream_(std::move(stream)),
        message_queue_(std::make_unique<MessageQueue>(*this)),
        closed_(false) {}

  /**
   * @brief Close the connection gracefully
   *
   * For SSL connections, performs async shutdown before closing the socket.
   * For plain TCP connections, immediately shuts down and closes the socket.
   */
  void DoClose() override {
    if (closed_) {
      return;
    }

    closed_ = true;

    if (!helper::GetLowestSocket(stream_).is_open()) {
      return;
    }

    if constexpr (helper::IsBeastSslStream<S>::value) {
      stream_.async_shutdown(boost::asio::bind_executor(
          GetExecutor(),
          [self = shared_from_this(), this](boost::system::error_code ec) {
            boost::system::error_code socket_ec;
            helper::GetLowestSocket(stream_).close(socket_ec);
          }));
    } else {
      boost::asio::post(GetStrand(), [self = shared_from_this(), this] {
        boost::system::error_code ec;
        helper::GetLowestSocket(stream_).shutdown(
            boost::asio::ip::tcp::socket::shutdown_both, ec);
        helper::GetLowestSocket(stream_).close(ec);
      });
    }
  }

  /**
   * @brief Check if the stream is still available
   * @return true if stream is open and not closed
   */
  bool IsStreamAvailable() const noexcept override { return !closed_; }

  /**
   * @brief Write complete HTTP response to client
   * @param resp HTTP response to write
   * @param keep_alive Whether to keep connection alive after writing
   */
  void DoWriteResponse(HttpResponse resp, bool keep_alive) override {
    if (!IsServerRunning() || !IsStreamAvailable()) {
      DoClose();
      return;
    }

    resp.keep_alive(keep_alive);
    resp.set(boost::beast::http::field::keep_alive,
             std::to_string(GetKeepAliveTimeout()));
    resp.prepare_payload();

    boost::beast::http::async_write(
        stream_, resp,
        boost::asio::bind_executor(
            GetExecutor(), [self = shared_from_this(), this, keep_alive](
                               boost::system::error_code ec,
                               [[maybe_unused]] std::size_t bytes_transfered) {
              if (ec) {
                DoClose();
              } else {
                if (!keep_alive) {
                  DoClose();
                } else {
                  DoCycle();
                }
              }
            }));
  }

  /**
   * @brief Flush response headers to client (manual mode)
   * @param header HTTP response headers
   */
  void DoFlushResponseHeader(
      boost::beast::http::response_header<boost::beast::http::fields> header)
      override {
    message_queue_->AddHeader(std::move(header));
  }

  /**
   * @brief Flush response body to client (manual mode)
   * @param body Response body content
   */
  void DoFlushResponseBody(std::string body) override {
    message_queue_->AddBody(std::move(body));
  }

 protected:
  /**
   * @brief Read HTTP request headers asynchronously
   */
  void DoReadHeader() override {
    if (!IsServerRunning() || !IsStreamAvailable()) {
      DoClose();
      return;
    }

    boost::beast::http::async_read_header(
        stream_, GetBuffer(), *GetParser(),
        boost::asio::bind_executor(
            GetExecutor(), [self = shared_from_this(), this](
                               boost::system::error_code ec,
                               [[maybe_unused]] std::size_t bytes_transfered) {
              if (ec) {
                DoClose();
              } else {
                DoRoute();
              }
            }));
  }

  /**
   * @brief Read HTTP request body asynchronously
   */
  void DoReadBody() override {
    if (!IsServerRunning() || !IsStreamAvailable()) {
      DoClose();
      return;
    }

    boost::beast::http::async_read(
        stream_, GetBuffer(), *GetParser(),
        boost::asio::bind_executor(
            GetExecutor(), [self = shared_from_this(), this](
                               boost::system::error_code ec,
                               [[maybe_unused]] std::size_t bytes_transfered) {
              if (ec) {
                DoClose();
              } else {
                MakeHttpServerTask();
              }
            }));
  }

 private:
  /**
   * @brief Thread-safe message queue for response streaming
   *
   * Manages a queue of response messages (headers and bodies) and ensures
   * they are written to the stream in the correct order. Supports both
   * automatic and manual response writing modes.
   */
  class MessageQueue {
   public:
    /**
     * @brief Construct a message queue for a connection
     * @param conn Reference to the parent connection
     */
    MessageQueue(HttpServerConnectionImpl<S>& conn)
        : conn_(conn), is_writing_(false) {}

    /**
     * @brief Add a body message to the queue
     * @param msg Response body content
     */
    void AddBody(std::string msg) {
      BodyMessage message = {std::make_shared<std::string>(std::move(msg))};
      boost::asio::post(conn_.GetExecutor(),
                        [conn = conn_.shared_from_this(), this,
                         message = std::move(message)] {
                          queue_.emplace_back(std::move(message));

                          if (!is_writing_) {
                            DoWrite();
                          }
                        });
    }

    /**
     * @brief Add a header message to the queue
     * @param header Response headers
     */
    void AddHeader(
        boost::beast::http::response_header<boost::beast::http::fields>
            header) {
      auto serializer =
          std::make_shared<boost::beast::http::response_serializer<
              boost::beast::http::empty_body>>(
              boost::beast::http::response<boost::beast::http::empty_body>{
                  header});
      boost::asio::post(conn_.GetExecutor(),
                        [conn = conn_.shared_from_this(), this,
                         serializer = std::move(serializer)] {
                          HeaderMessage message = {std::move(serializer)};
                          queue_.emplace_back(std::move(message));

                          if (!is_writing_) {
                            DoWrite();
                          }
                        });
    }

   private:
    /// @brief Body message containing response body content
    struct BodyMessage {
      std::shared_ptr<std::string> msg;  ///< Shared pointer to body content
    };

    /// @brief Header message containing response headers
    struct HeaderMessage {
      std::shared_ptr<boost::beast::http::response_serializer<
          boost::beast::http::empty_body>>
          msg;  ///< Shared pointer to header serializer
    };

    /**
     * @brief Process the message queue and write to stream
     */
    void DoWrite() {
      if (queue_.empty() || is_writing_) {
        return;
      }
      is_writing_ = true;
      auto& task = queue_.front();

      std::visit(
          [this, conn = std::static_pointer_cast<HttpServerConnectionImpl<S>>(
                     conn_.shared_from_this())](auto& task) {
            using T = std::decay_t<decltype(task)>;

            if constexpr (std::is_same_v<T, BodyMessage>) {
              auto buffers = boost::asio::buffer(*(task.msg));
              boost::asio::async_write(
                  conn->stream_, buffers,
                  boost::asio::bind_executor(conn->GetExecutor(),
                                             [msg = task.msg, conn, this](
                                                 boost::system::error_code ec,
                                                 [[maybe_unused]] std::size_t) {
                                               if (ec) {
                                                 conn->DoClose();
                                                 is_writing_ = false;
                                                 return;
                                               } else {
                                                 queue_.pop_front();
                                                 is_writing_ = false;
                                                 if (!queue_.empty()) {
                                                   DoWrite();
                                                 }
                                               }
                                             }));
            } else {
              boost::beast::http::async_write_header(
                  conn->stream_, *(task.msg),
                  boost::asio::bind_executor(
                      conn->GetExecutor(),
                      [conn, this](boost::system::error_code ec,
                                   [[maybe_unused]] std::size_t) {
                        if (ec) {
                          conn->DoClose();
                          is_writing_ = false;
                          return;
                        } else {
                          queue_.pop_front();
                          is_writing_ = false;
                          if (!queue_.empty()) {
                            DoWrite();
                          }
                        }
                      }));
            }
          },
          task);
    }

    std::deque<std::variant<BodyMessage, HeaderMessage>>
        queue_;                          ///< Message queue
    HttpServerConnectionImpl<S>& conn_;  ///< Parent connection
    std::atomic<bool> is_writing_;       ///< Write operation in progress flag
  };

  S stream_;  ///< Boost.Beast stream (TCP or SSL)
  std::unique_ptr<MessageQueue>
      message_queue_;         ///< Message queue for response streaming
  std::atomic<bool> closed_;  ///< Connection closed flag
};
}  // namespace connection_internal

}  // namespace bsrvcore

#endif
