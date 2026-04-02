#include <bsrvcore/allocator/allocator.h>
#include <bsrvcore/oai/completion/oai_completion.h>

#include <boost/asio/io_context.hpp>
#include <cstdlib>
#include <future>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace completion = bsrvcore::oai::completion;

struct Options {
  std::string base_url;
  std::string api_key;
  std::string model;
  std::string organization;
  std::string project;
  std::string system;
  std::string user;
  bool stream{false};
  bool print_reasoning{false};
};

static void PrintUsage(const char* argv0) {
  std::cerr
      << "Usage: " << argv0 << " [options]\n\n"
      << "Required:\n"
      << "  --base_url <url>     Provider base URL (e.g. "
         "https://api.openai.com/v1)\n"
      << "  --model <name>       Model name\n"
      << "  --user <text>        User prompt\n\n"
      << "Optional:\n"
      << "  --api_key <key>      API key (or use env OAI_API_KEY)\n"
      << "  --organization <org> OpenAI-Organization header value\n"
      << "  --project <proj>     OpenAI-Project header value\n"
      << "  --system <text>      System prompt\n"
      << "  --stream             Enable SSE streaming mode\n"
      << "  --print_reasoning    Print assistant reasoning_content to stderr\n"
      << "  -h, --help           Show this help\n";
}

static bool ReadValueArg(int* index, int argc, char** argv,
                         std::string_view arg, std::string_view name,
                         std::string* out, std::string* error_out) {
  if (arg == name) {
    if ((*index) + 1 >= argc) {
      if (error_out != nullptr) {
        *error_out = "missing value for " + std::string(name);
      }
      return false;
    }
    *out = argv[++(*index)];
    return true;
  }

  const auto prefix = std::string(name) + "=";
  if (arg.size() > prefix.size() && arg.starts_with(prefix)) {
    *out = std::string(arg.substr(prefix.size()));
    return true;
  }

  return false;
}

static bool ParseArgs(int argc, char** argv, Options* out,
                      std::string* error_out) {
  if (out == nullptr) {
    return false;
  }

  for (int i = 1; i < argc; ++i) {
    std::string_view const arg(argv[i]);

    if (arg == "-h" || arg == "--help") {
      return false;
    }

    if (arg == "--stream") {
      out->stream = true;
      continue;
    }

    if (arg == "--print_reasoning") {
      out->print_reasoning = true;
      continue;
    }

    if (ReadValueArg(&i, argc, argv, arg, "--base_url", &out->base_url,
                     error_out)) {
      continue;
    }
    if (ReadValueArg(&i, argc, argv, arg, "--model", &out->model, error_out)) {
      continue;
    }
    if (ReadValueArg(&i, argc, argv, arg, "--user", &out->user, error_out)) {
      continue;
    }
    if (ReadValueArg(&i, argc, argv, arg, "--system", &out->system,
                     error_out)) {
      continue;
    }
    if (ReadValueArg(&i, argc, argv, arg, "--api_key", &out->api_key,
                     error_out)) {
      continue;
    }
    if (ReadValueArg(&i, argc, argv, arg, "--organization", &out->organization,
                     error_out)) {
      continue;
    }
    if (ReadValueArg(&i, argc, argv, arg, "--project", &out->project,
                     error_out)) {
      continue;
    }

    if (error_out != nullptr) {
      *error_out = "unknown argument: " + std::string(arg);
    }
    return false;
  }

  if (out->api_key.empty()) {
    if (const char* env = std::getenv("OAI_API_KEY");
        env != nullptr && (*env != 0)) {
      out->api_key = env;
    }
  }

  if (out->base_url.empty() || out->model.empty() || out->user.empty()) {
    if (error_out != nullptr) {
      *error_out = "--base_url, --model and --user are required";
    }
    return false;
  }

  return true;
}

int main(int argc, char** argv) {
  Options options;
  std::string error;
  if (!ParseArgs(argc, argv, &options, &error)) {
    if (!error.empty()) {
      std::cerr << "Error: " << error << "\n\n";
    }
    PrintUsage(argv[0]);
    return error.empty() ? 0 : 2;
  }

  boost::asio::io_context ioc;

  auto info = bsrvcore::AllocateShared<completion::OaiCompletionInfo>();
  info->base_url = options.base_url;
  info->api_key = options.api_key;
  if (!options.organization.empty()) {
    info->organization = options.organization;
  }
  if (!options.project.empty()) {
    info->project = options.project;
  }

  auto model_info = bsrvcore::AllocateShared<completion::OaiModelInfo>();
  model_info->model = options.model;

  completion::OaiCompletionFactory const factory(ioc.get_executor(), info);

  completion::OaiCompletionFactory::StatePtr state = nullptr;
  if (!options.system.empty()) {
    completion::OaiMessage system;
    system.role = "system";
    system.message = options.system;
    state = factory.AppendMessage(system, state);
  }
  {
    completion::OaiMessage user;
    user.role = "user";
    user.message = options.user;
    state = factory.AppendMessage(user, state);
  }

  if (!options.stream) {
    std::promise<completion::OaiCompletionFactory::StatePtr> promise;
    auto future = promise.get_future();
    bool fulfilled = false;

    const bool started = factory.FetchCompletion(
        state, model_info,
        [&promise, &fulfilled](
            completion::OaiCompletionFactory::StatePtr done_state) mutable {
          if (fulfilled) {
            return;
          }
          fulfilled = true;
          promise.set_value(std::move(done_state));
        });
    if (!started) {
      std::cerr << "Failed to start completion request.\n";
      return 1;
    }

    ioc.run();
    auto done = future.get();
    if (!done) {
      std::cerr << "No result state.\n";
      return 1;
    }

    const auto& log = done->GetLog();
    const auto& msg = done->GetMessage();

    std::cout << msg.message << "\n";

    if (options.print_reasoning && !msg.reasoning.empty()) {
      std::cerr << "reasoning:\n" << msg.reasoning << "\n";
    }

    std::cerr << "status="
              << (log.status == completion::OaiCompletionStatus::kSuccess
                      ? "success"
                      : "fail")
              << " http=" << log.http_status_code;
    if (!log.request_id.empty()) {
      std::cerr << " request_id=" << log.request_id;
    }
    if (!log.finish_reason.empty()) {
      std::cerr << " finish_reason=" << log.finish_reason;
    }
    if (!log.error_message.empty()) {
      std::cerr << " error=\"" << log.error_message << "\"";
    }
    std::cerr << "\n";
    return log.status == completion::OaiCompletionStatus::kSuccess ? 0 : 1;
  }

  // Stream mode
  std::promise<completion::OaiCompletionFactory::StatePtr> done_promise;
  auto done_future = done_promise.get_future();
  bool fulfilled = false;

  auto streamed_reasoning_started = bsrvcore::AllocateShared<bool>(false);

  completion::OaiCompletionFactory::StreamDeltaCallback on_reasoning_delta;
  if (options.print_reasoning) {
    on_reasoning_delta =
        [streamed_reasoning_started](const std::string& reasoning_delta) {
          if (!*streamed_reasoning_started) {
            *streamed_reasoning_started = true;
            std::cerr << "reasoning:\n";
          }
          std::cerr << reasoning_delta << std::flush;
        };
  }

  auto on_done =
      [&done_promise, &fulfilled](
          completion::OaiCompletionFactory::StatePtr done_state) mutable {
        if (fulfilled) {
          return;
        }
        fulfilled = true;
        done_promise.set_value(std::move(done_state));
      };

  completion::OaiCompletionFactory::StreamDeltaCallback const on_delta =
      [](const std::string& delta) { std::cout << delta << std::flush; };

  bool started = false;
  if (on_reasoning_delta) {
    started = factory.FetchStreamCompletion(state, model_info, on_done,
                                            on_delta, on_reasoning_delta);
  } else {
    started =
        factory.FetchStreamCompletion(state, model_info, on_done, on_delta);
  }

  if (!started) {
    std::cerr << "Failed to start streaming request.\n";
    return 1;
  }

  ioc.run();
  auto done = done_future.get();
  std::cout << "\n";

  if (!done) {
    std::cerr << "No result state.\n";
    return 1;
  }

  const auto& log = done->GetLog();

  const bool streamed_reasoning =
      options.print_reasoning && *streamed_reasoning_started;
  if (streamed_reasoning) {
    std::cerr << "\n";
  } else if (options.print_reasoning && !done->GetMessage().reasoning.empty()) {
    std::cerr << "reasoning:\n" << done->GetMessage().reasoning << "\n";
  }

  std::cerr << "status="
            << (log.status == completion::OaiCompletionStatus::kSuccess
                    ? "success"
                    : "fail")
            << " http=" << log.http_status_code
            << " deltas=" << log.delta_count;
  if (!log.request_id.empty()) {
    std::cerr << " request_id=" << log.request_id;
  }
  if (!log.finish_reason.empty()) {
    std::cerr << " finish_reason=" << log.finish_reason;
  }
  if (!log.error_message.empty()) {
    std::cerr << " error=\"" << log.error_message << "\"";
  }
  std::cerr << "\n";

  return log.status == completion::OaiCompletionStatus::kSuccess ? 0 : 1;
}
