#include "benchmark_scenarios.h"

#include <algorithm>
#include <boost/beast/core/string.hpp>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/route/http_request_method.h"
#include "bsrvcore/session/attribute.h"
#include "bsrvcore/session/context.h"

namespace bsrvcore::benchmark {

namespace {

struct CounterAttribute : CloneableAttribute<CounterAttribute> {
  explicit CounterAttribute(std::uint64_t v = 0) : value(v) {}

  std::string ToString() const override { return std::to_string(value); }

  bool Equals(const Attribute& another) const noexcept override {
    const auto* other = dynamic_cast<const CounterAttribute*>(&another);
    return other != nullptr && other->value == value;
  }

  std::size_t Hash() const noexcept override {
    return std::hash<std::uint64_t>()(value);
  }

  std::uint64_t value = 0;
};

bool HasHeader(const BenchmarkHttpResponse& response, std::string_view key) {
  for (const auto& field : response.base()) {
    if (boost::beast::iequals(field.name_string(), key)) {
      return true;
    }
  }
  return false;
}

std::string HeaderValue(const BenchmarkHttpResponse& response,
                        std::string_view key) {
  for (const auto& field : response.base()) {
    if (boost::beast::iequals(field.name_string(), key)) {
      return std::string(field.value());
    }
  }
  return {};
}

}  // namespace

std::vector<ScenarioDefinition> BuildScenarios() {
  auto make_static_get = []() -> ScenarioDefinition {
    ScenarioDefinition scenario;
    scenario.name = "http_get_static";
    scenario.summary = "GET /ping returning a small text body";
    scenario.io_focused = true;
    scenario.configure_server = [](HttpServer& server) {
      server.AddRouteEntry(
          HttpRequestMethod::kGet, "/ping",
          [](std::shared_ptr<HttpServerTask> task) {
            task->SetKeepAlive(task->GetRequest().keep_alive());
            task->GetResponse().result(http::status::ok);
            task->SetField(http::field::content_type,
                           "text/plain; charset=utf-8");
            task->SetBody("pong");
          });
    };
    scenario.make_request = [](WorkerState&) {
      RequestSpec request;
      request.method = http::verb::get;
      request.target = "/ping";
      return request;
    };
    scenario.validate_response = [](const BenchmarkHttpResponse& response,
                                    WorkerState&, std::string& error) {
      if (response.result() != http::status::ok || response.body() != "pong") {
        error = "expected 200/pong";
        return false;
      }
      return true;
    };
    return scenario;
  };

  auto make_route_param = []() -> ScenarioDefinition {
    ScenarioDefinition scenario;
    scenario.name = "http_get_route_param";
    scenario.summary = "GET /users/{id} with path parameter extraction";
    scenario.configure_server = [](HttpServer& server) {
      server.AddRouteEntry(
          HttpRequestMethod::kGet, "/users/{id}",
          [](std::shared_ptr<HttpServerTask> task) {
            task->SetKeepAlive(task->GetRequest().keep_alive());
            task->GetResponse().result(http::status::ok);
            task->SetField(http::field::content_type,
                           "text/plain; charset=utf-8");
            const auto* id = task->GetPathParameter("id");
            if (id == nullptr) {
              task->GetResponse().result(http::status::bad_request);
              task->SetBody("missing-id");
              return;
            }
            task->SetBody(*id);
          });
    };
    scenario.make_request = [](WorkerState& state) {
      RequestSpec request;
      request.method = http::verb::get;
      request.target = "/users/w" + std::to_string(state.worker_index) + "-" +
                       std::to_string(state.request_index);
      return request;
    };
    scenario.validate_response = [](const BenchmarkHttpResponse& response,
                                    WorkerState& state, std::string& error) {
      const std::string expected = "w" + std::to_string(state.worker_index) +
                                   "-" + std::to_string(state.request_index);
      if (response.result() != http::status::ok ||
          response.body() != expected) {
        error = "route param response mismatch";
        return false;
      }
      ++state.request_index;
      return true;
    };
    return scenario;
  };

  auto make_global_aspect = []() -> ScenarioDefinition {
    ScenarioDefinition scenario;
    scenario.name = "http_get_global_aspect";
    scenario.summary = "GET /ping with one global pre/post aspect pair";
    scenario.configure_server = [](HttpServer& server) {
      server.AddGlobalAspect(
          [](std::shared_ptr<HttpPreServerTask> task) {
            task->SetField("X-Bench-Aspect-Pre", "1");
          },
          [](std::shared_ptr<HttpPostServerTask> task) {
            task->SetField("X-Bench-Aspect-Post", "1");
          });
      server.AddRouteEntry(
          HttpRequestMethod::kGet, "/ping",
          [](std::shared_ptr<HttpServerTask> task) {
            task->SetKeepAlive(task->GetRequest().keep_alive());
            task->GetResponse().result(http::status::ok);
            task->SetField(http::field::content_type,
                           "text/plain; charset=utf-8");
            task->SetBody("pong");
          });
    };
    scenario.make_request = [](WorkerState&) {
      RequestSpec request;
      request.method = http::verb::get;
      request.target = "/ping";
      return request;
    };
    scenario.validate_response = [](const BenchmarkHttpResponse& response,
                                    WorkerState&, std::string& error) {
      if (response.result() != http::status::ok || response.body() != "pong" ||
          !HasHeader(response, "X-Bench-Aspect-Pre") ||
          !HasHeader(response, "X-Bench-Aspect-Post")) {
        error = "aspect response mismatch";
        return false;
      }
      return true;
    };
    return scenario;
  };

  auto make_long_aspect_chain = []() -> ScenarioDefinition {
    constexpr std::size_t kAspectCount = 64;

    ScenarioDefinition scenario;
    scenario.name = "http_get_aspect_chain_64";
    scenario.summary =
        "GET /ping with 64 global aspects to validate pre/post order";
    scenario.configure_server = [](HttpServer& server) {
      for (std::size_t i = 0; i < kAspectCount; ++i) {
        server.AddGlobalAspect(
            [i](std::shared_ptr<HttpPreServerTask> task) {
              task->SetField("X-Bench-Aspect-Pre-Last", std::to_string(i));
            },
            [i](std::shared_ptr<HttpPostServerTask> task) {
              task->SetField("X-Bench-Aspect-Post-Last", std::to_string(i));
            });
      }

      server.AddRouteEntry(
          HttpRequestMethod::kGet, "/ping",
          [](std::shared_ptr<HttpServerTask> task) {
            task->SetKeepAlive(task->GetRequest().keep_alive());
            task->GetResponse().result(http::status::ok);
            task->SetField(http::field::content_type,
                           "text/plain; charset=utf-8");
            task->SetBody("pong");
          });
    };
    scenario.make_request = [](WorkerState&) {
      RequestSpec request;
      request.method = http::verb::get;
      request.target = "/ping";
      return request;
    };
    scenario.validate_response = [](const BenchmarkHttpResponse& response,
                                    WorkerState&, std::string& error) {
      if (response.result() != http::status::ok || response.body() != "pong") {
        error = "expected 200/pong";
        return false;
      }

      const auto pre_last = HeaderValue(response, "X-Bench-Aspect-Pre-Last");
      const auto post_last = HeaderValue(response, "X-Bench-Aspect-Post-Last");
      if (pre_last != "63" || post_last != "0") {
        error = "long aspect chain order mismatch";
        return false;
      }

      return true;
    };
    return scenario;
  };

  auto make_post_echo = [](std::string name, std::size_t size,
                           char fill) -> ScenarioDefinition {
    ScenarioDefinition scenario;
    scenario.name = std::move(name);
    scenario.summary = "POST /echo returning the request body";
    scenario.io_focused = true;
    scenario.required_max_body_size =
        std::max<std::size_t>(size * 2, 256 * 1024);
    scenario.configure_server = [](HttpServer& server) {
      server.AddRouteEntry(
          HttpRequestMethod::kPost, "/echo",
          [](std::shared_ptr<HttpServerTask> task) {
            task->SetKeepAlive(task->GetRequest().keep_alive());
            task->GetResponse().result(http::status::ok);
            task->SetField(http::field::content_type,
                           "text/plain; charset=utf-8");
            task->SetBody(task->GetRequest().body());
          });
    };
    const std::string payload(size, fill);
    scenario.make_request = [payload](WorkerState&) {
      RequestSpec request;
      request.method = http::verb::post;
      request.target = "/echo";
      request.body = payload;
      request.headers.emplace_back(http::field::content_type, "text/plain");
      return request;
    };
    scenario.validate_response = [payload](
                                     const BenchmarkHttpResponse& response,
                                     WorkerState&, std::string& error) {
      if (response.result() != http::status::ok || response.body() != payload) {
        error = "echo response mismatch";
        return false;
      }
      return true;
    };
    return scenario;
  };

  auto make_session_counter = []() -> ScenarioDefinition {
    ScenarioDefinition scenario;
    scenario.name = "http_session_counter";
    scenario.summary =
        "GET /session increments a counter in the session context";
    scenario.prime_each_worker = true;
    scenario.configure_server = [](HttpServer& server) {
      server.SetDefaultSessionTimeout(60000);
      server.AddRouteEntry(
          HttpRequestMethod::kGet, "/session",
          [](std::shared_ptr<HttpServerTask> task) {
            task->SetKeepAlive(task->GetRequest().keep_alive());
            task->GetResponse().result(http::status::ok);
            task->SetField(http::field::content_type,
                           "text/plain; charset=utf-8");
            auto session = task->GetSession();
            if (!session) {
              task->GetResponse().result(http::status::internal_server_error);
              task->SetBody("missing-session");
              return;
            }

            std::uint64_t next_value = 1;
            auto current = std::dynamic_pointer_cast<CounterAttribute>(
                session->GetAttribute("counter"));
            if (current) {
              next_value = current->value + 1;
            }

            session->SetAttribute(
                "counter",
                bsrvcore::AllocateShared<CounterAttribute>(next_value));
            task->SetBody(std::to_string(next_value));
          });
    };
    scenario.make_request = [](WorkerState&) {
      RequestSpec request;
      request.method = http::verb::get;
      request.target = "/session";
      return request;
    };
    scenario.validate_response = [](const BenchmarkHttpResponse& response,
                                    WorkerState& state, std::string& error) {
      if (response.result() != http::status::ok) {
        error = "expected 200 for session request";
        return false;
      }
      if (state.request_index == 0 && state.cookie_jar.empty()) {
        error = "expected session cookie after first request";
        return false;
      }

      std::uint64_t actual = 0;
      try {
        actual = static_cast<std::uint64_t>(std::stoull(response.body()));
      } catch (...) {
        error = "session response is not an integer";
        return false;
      }

      const std::uint64_t expected = state.request_index + 1;
      if (actual != expected) {
        error = "session counter mismatch";
        return false;
      }

      ++state.request_index;
      return true;
    };
    return scenario;
  };

  return {make_static_get(),
          make_route_param(),
          make_global_aspect(),
          make_long_aspect_chain(),
          make_post_echo("http_post_echo_1k", 1024, 'x'),
          make_post_echo("http_post_echo_64k", 64 * 1024, 'y'),
          make_session_counter()};
}

const ScenarioDefinition& FindScenario(
    const std::vector<ScenarioDefinition>& scenarios, std::string_view name) {
  auto it = std::find_if(scenarios.begin(), scenarios.end(),
                         [name](const ScenarioDefinition& scenario) {
                           return scenario.name == name;
                         });
  if (it == scenarios.end()) {
    throw std::invalid_argument("Unknown scenario: " + std::string(name));
  }
  return *it;
}

std::vector<const ScenarioDefinition*> ResolveSelectedScenarios(
    const std::vector<ScenarioDefinition>& scenarios, const CliConfig& cli) {
  if (cli.scenario_name == "io") {
    std::vector<const ScenarioDefinition*> selected;
    for (const auto& scenario : scenarios) {
      if (scenario.io_focused) {
        selected.push_back(&scenario);
      }
    }
    if (selected.empty()) {
      throw std::invalid_argument("No IO-focused scenarios are defined");
    }
    return selected;
  }

  if (cli.scenario_name == "all") {
    std::vector<const ScenarioDefinition*> selected;
    selected.reserve(scenarios.size());
    for (const auto& scenario : scenarios) {
      selected.push_back(&scenario);
    }
    return selected;
  }

  return {&FindScenario(scenarios, cli.scenario_name)};
}

}  // namespace bsrvcore::benchmark
