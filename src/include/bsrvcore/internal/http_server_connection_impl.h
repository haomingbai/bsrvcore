/**
 * @file http_server_connection_impl.h
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-01
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

#include <atomic>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/fields_fwd.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/http/serializer_fwd.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
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

template <typename T>
struct IsBeastSslStream : std::false_type {};
template <typename NextLayer>
struct IsBeastSslStream<boost::beast::ssl_stream<NextLayer>> : std::true_type {
};

inline boost::asio::ip::tcp::socket& GetLowestSocket(
    boost::beast::tcp_stream& s) {
  return s.socket();
}
inline boost::asio::ip::tcp::socket& GetLowestSocket(
    boost::beast::ssl_stream<boost::beast::tcp_stream>& s) {
  return s.next_layer().socket();
}

}  // namespace helper

template <typename S>
concept ValidStream = requires(S s) {
  { helper::GetLowestSocket(s) };
};

template <ValidStream S>
class HttpServerConnectionImpl : public HttpServerConnection {
 public:
  HttpServerConnectionImpl(
      S stream,
      boost::asio::strand<boost::asio::io_context::executor_type> strand,
      std::shared_ptr<HttpServer> srv, std::size_t header_read_expiry,
      std::size_t keep_alive_timeout)
      : HttpServerConnection(std::move(strand), std::move(srv),
                             header_read_expiry, keep_alive_timeout),
        stream_(std::move(stream)),
        message_queue_(std::make_unique<MessageQueue>(*this)),
        closed_(false) {}

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

  bool IsStreamAvailable() const noexcept override { return !closed_; }

  void DoWriteResponse(HttpResponse resp, bool keep_alive) override {
    if (!IsServerRunning() || !IsStreamAvailable()) {
      DoClose();
      return;
    }

    boost::beast::http::async_write(
        stream_, GetBuffer(), resp,
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

  void DoFlushResponseHeader(
      boost::beast::http::response_header<boost::beast::http::fields> header)
      override {
    message_queue_->AddHeader(std::move(header));
  }

  void DoFlushResponseBody(std::string body) override {
    message_queue_->AddBody(std::move(body));
  }

 protected:
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
  class MessageQueue {
   public:
    MessageQueue(HttpServerConnectionImpl<S>& conn)
        : conn_(conn), is_writing_(false) {}

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

    void AddHeader(
        boost::beast::http::response_header<boost::beast::http::fields>
            header) {
      auto serializer =
          std::make_shared<boost::beast::http::response_serializer<
              boost::beast::http::empty_body>>(std::move(header));
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
    struct BodyMessage {
      std::shared_ptr<std::string> msg;
    };

    struct HeaderMessage {
      std::shared_ptr<boost::beast::http::response_serializer<
          boost::beast::http::empty_body>>
          msg;
    };

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

    std::deque<std::variant<BodyMessage, HeaderMessage>> queue_;
    HttpServerConnectionImpl<S>& conn_;
    std::atomic<bool> is_writing_;
  };

  S stream_;
  std::unique_ptr<MessageQueue> message_queue_;
  std::atomic<bool> closed_;
};
}  // namespace connection_internal

}  // namespace bsrvcore
