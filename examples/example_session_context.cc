/**
 * @file example_session_context.cc
 * @brief Session + Context + Attribute usage.
 *
 * Demonstrates:
 * - Session creation via GetSessionId
 * - Context attribute storage and retrieval
 * - CloneableAttribute for typed data
 *
 * Prerequisites: Boost, OpenSSL (required by bsrvcore build).
 * Build: cmake -S . -B build -DBSRVCORE_BUILD_EXAMPLES=ON
 * Run: ./build/examples/example_session_context
 */

// BEGIN README_SNIPPET: session_context
#include <bsrvcore/bsrvcore.h>

#include <boost/asio/ip/address.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>

#include <iostream>
#include <memory>
#include <string>

class UserAttribute : public bsrvcore::CloneableAttribute<UserAttribute> {
 public:
  explicit UserAttribute(std::string name) : name_(std::move(name)) {}

  std::string ToString() const override { return name_; }

  std::string name_;
};

int main() {
  auto server = std::make_unique<bsrvcore::HttpServer>(2);
  server
      ->AddRouteEntry(
          bsrvcore::HttpRequestMethod::kGet, "/session",
          [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
            const std::string& session_id = task->GetSessionId();
            auto session = task->GetSession();

            if (session && !session->HasAttribute("user")) {
              session->SetAttribute("user",
                                    std::make_shared<UserAttribute>("guest"));
            }

            std::string user_name = "unknown";
            if (session) {
              auto attr = session->GetAttribute("user");
              auto user = std::dynamic_pointer_cast<UserAttribute>(attr);
              if (user) {
                user_name = user->ToString();
              }
            }

            task->GetResponse().result(boost::beast::http::status::ok);
            task->SetField(boost::beast::http::field::content_type,
                           "text/plain; charset=utf-8");
            task->SetBody("sessionId=" + session_id + "\nuser=" + user_name +
                          "\n");
          })
      ->AddListen({boost::asio::ip::make_address("0.0.0.0"), 8085});

  if (!server->Start(1)) {
    std::cerr << "Failed to start server." << std::endl;
    return 1;
  }

  std::cout << "Listening on http://0.0.0.0:8085/session" << std::endl;
  std::cout << "Press Enter to stop." << std::endl;
  std::cin.get();

  server->Stop();
  return 0;
}
// END README_SNIPPET: session_context
