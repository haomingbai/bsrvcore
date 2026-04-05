#include <gtest/gtest.h>

#include <boost/asio/thread_pool.hpp>
#include <memory>
#include <type_traits>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/bsrvrun/http_request_aspect_handler_factory.h"
#include "bsrvcore/bsrvrun/http_request_handler_factory.h"
#include "bsrvcore/bsrvrun/parameter_map.h"
#include "bsrvcore/bsrvrun/string.h"
#include "bsrvcore/connection/client/http_client_session.h"
#include "bsrvcore/connection/client/http_client_session_attribute.h"
#include "bsrvcore/connection/client/http_client_task.h"
#include "bsrvcore/connection/client/http_sse_client_task.h"
#include "bsrvcore/connection/client/multipart_generator.h"
#include "bsrvcore/connection/client/put_generator.h"
#include "bsrvcore/connection/client/sse_event_parser.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/connection/server/multipart_parser.h"
#include "bsrvcore/connection/server/put_processor.h"
#include "bsrvcore/core/async_waiter.h"
#include "bsrvcore/core/atomic_shared_ptr.h"
#include "bsrvcore/core/blue_print.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/core/logger.h"
#include "bsrvcore/file/file_reader.h"
#include "bsrvcore/file/file_state.h"
#include "bsrvcore/file/file_writer.h"
#include "bsrvcore/route/cloneable_http_request_aspect_handler.h"
#include "bsrvcore/route/cloneable_http_request_handler.h"
#include "bsrvcore/route/http_request_aspect_handler.h"
#include "bsrvcore/route/http_request_handler.h"
#include "bsrvcore/route/http_route_result.h"
#include "bsrvcore/session/attribute.h"
#include "bsrvcore/session/context.h"

namespace {

class DummyLogger final : public bsrvcore::Logger {
 public:
  void Log([[maybe_unused]] bsrvcore::LogLevel level,
           [[maybe_unused]] std::string message) override {}
};

class DummyParameterMap final : public bsrvcore::bsrvrun::ParameterMap {
 public:
  [[nodiscard]] bsrvcore::bsrvrun::String Get(
      [[maybe_unused]] const bsrvcore::bsrvrun::String& key) const override {
    return bsrvcore::bsrvrun::String();
  }

  void Set([[maybe_unused]] const bsrvcore::bsrvrun::String& key,
           [[maybe_unused]] const bsrvcore::bsrvrun::String& value) override {}
};

class DummyHandler final : public bsrvcore::HttpRequestHandler {
 public:
  void Service([[maybe_unused]] std::shared_ptr<bsrvcore::HttpServerTask> task)
      override {}
};

class DummyAspect final : public bsrvcore::HttpRequestAspectHandler {
 public:
  void PreService([[maybe_unused]] std::shared_ptr<bsrvcore::HttpPreServerTask>
                      task) override {}

  void PostService(
      [[maybe_unused]] std::shared_ptr<bsrvcore::HttpPostServerTask> task)
      override {}
};

class DummyCloneableHandler final
    : public bsrvcore::CloneableHttpRequestHandler {
 public:
  void Service([[maybe_unused]] std::shared_ptr<bsrvcore::HttpServerTask> task)
      override {}

  [[nodiscard]] bsrvcore::OwnedPtr<bsrvcore::CloneableHttpRequestHandler>
  Clone() const override {
    return bsrvcore::AllocateUnique<DummyCloneableHandler>();
  }
};

class DummyCloneableAspect final
    : public bsrvcore::CloneableHttpRequestAspectHandler {
 public:
  void PreService([[maybe_unused]] std::shared_ptr<bsrvcore::HttpPreServerTask>
                      task) override {}

  void PostService(
      [[maybe_unused]] std::shared_ptr<bsrvcore::HttpPostServerTask> task)
      override {}

  [[nodiscard]] bsrvcore::OwnedPtr<bsrvcore::CloneableHttpRequestAspectHandler>
  Clone() const override {
    return bsrvcore::AllocateUnique<DummyCloneableAspect>();
  }
};

class DummyAttribute final
    : public bsrvcore::CloneableAttribute<DummyAttribute> {
 public:
  DummyAttribute() = default;
};

class DummyHandlerFactory final
    : public bsrvcore::bsrvrun::HttpRequestHandlerFactory {
 public:
  [[nodiscard]] bsrvcore::OwnedPtr<bsrvcore::HttpRequestHandler> Get(
      [[maybe_unused]] bsrvcore::bsrvrun::ParameterMap* parameters) override {
    return {};
  }
};

class DummyAspectFactory final
    : public bsrvcore::bsrvrun::HttpRequestAspectHandlerFactory {
 public:
  [[nodiscard]] bsrvcore::OwnedPtr<bsrvcore::HttpRequestAspectHandler> Get(
      [[maybe_unused]] bsrvcore::bsrvrun::ParameterMap* parameters) override {
    return {};
  }
};

static_assert(
    std::is_base_of_v<bsrvcore::CopyableMovable<bsrvcore::Allocator<int>>,
                      bsrvcore::Allocator<int>>);
static_assert(
    std::is_base_of_v<bsrvcore::CopyableMovable<bsrvcore::OwnedDeleter>,
                      bsrvcore::OwnedDeleter>);
static_assert(
    std::is_base_of_v<bsrvcore::CopyableMovable<bsrvcore::bsrvrun::String>,
                      bsrvcore::bsrvrun::String>);
static_assert(std::is_base_of_v<
              bsrvcore::NonCopyableNonMovable<bsrvcore::AtomicSharedPtr<int>>,
              bsrvcore::AtomicSharedPtr<int>>);
static_assert(std::is_base_of_v<bsrvcore::MovableOnly<bsrvcore::BluePrint>,
                                bsrvcore::BluePrint>);
static_assert(
    std::is_base_of_v<bsrvcore::MovableOnly<bsrvcore::ReuseableBluePrint>,
                      bsrvcore::ReuseableBluePrint>);
static_assert(std::is_base_of_v<
              bsrvcore::NonCopyableNonMovable<bsrvcore::BluePrintFactory>,
              bsrvcore::BluePrintFactory>);
static_assert(std::is_base_of_v<
              bsrvcore::CopyableMovable<bsrvcore::HttpServerRuntimeOptions>,
              bsrvcore::HttpServerRuntimeOptions>);
static_assert(
    std::is_base_of_v<bsrvcore::NonCopyableNonMovable<bsrvcore::Logger>,
                      bsrvcore::Logger>);
static_assert(std::is_base_of_v<
              bsrvcore::CopyableMovable<bsrvcore::bsrvrun::ParameterMap>,
              bsrvcore::bsrvrun::ParameterMap>);
static_assert(
    std::is_base_of_v<bsrvcore::NonCopyableNonMovable<
                          bsrvcore::bsrvrun::HttpRequestHandlerFactory>,
                      bsrvcore::bsrvrun::HttpRequestHandlerFactory>);
static_assert(
    std::is_base_of_v<bsrvcore::NonCopyableNonMovable<
                          bsrvcore::bsrvrun::HttpRequestAspectHandlerFactory>,
                      bsrvcore::bsrvrun::HttpRequestAspectHandlerFactory>);
static_assert(std::is_base_of_v<bsrvcore::CopyableMovable<bsrvcore::Attribute>,
                                bsrvcore::Attribute>);
static_assert(
    std::is_base_of_v<
        bsrvcore::CopyableMovable<bsrvcore::CloneableAttribute<DummyAttribute>>,
        bsrvcore::CloneableAttribute<DummyAttribute>>);
static_assert(std::is_base_of_v<
              bsrvcore::NonCopyableNonMovable<bsrvcore::HttpRequestHandler>,
              bsrvcore::HttpRequestHandler>);
static_assert(
    std::is_base_of_v<
        bsrvcore::NonCopyableNonMovable<bsrvcore::HttpRequestAspectHandler>,
        bsrvcore::HttpRequestAspectHandler>);
static_assert(
    std::is_base_of_v<
        bsrvcore::NonCopyableNonMovable<bsrvcore::CloneableHttpRequestHandler>,
        bsrvcore::CloneableHttpRequestHandler>);
static_assert(
    std::is_base_of_v<bsrvcore::NonCopyableNonMovable<
                          bsrvcore::CloneableHttpRequestAspectHandler>,
                      bsrvcore::CloneableHttpRequestAspectHandler>);
static_assert(
    std::is_base_of_v<bsrvcore::CopyableMovable<bsrvcore::HttpRouteResult>,
                      bsrvcore::HttpRouteResult>);
static_assert(
    std::is_base_of_v<bsrvcore::NonCopyableNonMovable<bsrvcore::HttpTaskBase>,
                      bsrvcore::HttpTaskBase>);
static_assert(
    std::is_base_of_v<bsrvcore::CopyableMovable<bsrvcore::HttpClientOptions>,
                      bsrvcore::HttpClientOptions>);
static_assert(
    std::is_base_of_v<bsrvcore::CopyableMovable<bsrvcore::HttpClientResult>,
                      bsrvcore::HttpClientResult>);
static_assert(
    std::is_base_of_v<bsrvcore::NonCopyableNonMovable<bsrvcore::HttpClientTask>,
                      bsrvcore::HttpClientTask>);
static_assert(
    std::is_base_of_v<bsrvcore::CopyableMovable<bsrvcore::HttpSseClientOptions>,
                      bsrvcore::HttpSseClientOptions>);
static_assert(
    std::is_base_of_v<bsrvcore::CopyableMovable<bsrvcore::HttpSseClientResult>,
                      bsrvcore::HttpSseClientResult>);
static_assert(std::is_base_of_v<
              bsrvcore::NonCopyableNonMovable<bsrvcore::HttpSseClientTask>,
              bsrvcore::HttpSseClientTask>);
static_assert(std::is_base_of_v<
              bsrvcore::NonCopyableNonMovable<bsrvcore::HttpClientSession>,
              bsrvcore::HttpClientSession>);
static_assert(std::is_base_of_v<
              bsrvcore::CopyableMovable<bsrvcore::HttpClientSessionAttribute>,
              bsrvcore::HttpClientSessionAttribute>);
static_assert(std::is_base_of_v<bsrvcore::CopyableMovable<bsrvcore::SseEvent>,
                                bsrvcore::SseEvent>);
static_assert(
    std::is_base_of_v<bsrvcore::NonCopyableNonMovable<bsrvcore::SseEventParser>,
                      bsrvcore::SseEventParser>);
static_assert(
    std::is_base_of_v<bsrvcore::NonCopyableNonMovable<bsrvcore::FileReader>,
                      bsrvcore::FileReader>);
static_assert(
    std::is_base_of_v<bsrvcore::NonCopyableNonMovable<bsrvcore::FileWriter>,
                      bsrvcore::FileWriter>);
static_assert(std::is_base_of_v<
              bsrvcore::NonCopyableNonMovable<bsrvcore::FileReadingState>,
              bsrvcore::FileReadingState>);
static_assert(std::is_base_of_v<
              bsrvcore::NonCopyableNonMovable<bsrvcore::FileWritingState>,
              bsrvcore::FileWritingState>);
static_assert(
    std::is_base_of_v<bsrvcore::NonCopyableNonMovable<bsrvcore::PutProcessor>,
                      bsrvcore::PutProcessor>);
static_assert(std::is_base_of_v<
              bsrvcore::NonCopyableNonMovable<bsrvcore::MultipartParser>,
              bsrvcore::MultipartParser>);
static_assert(
    std::is_base_of_v<bsrvcore::NonCopyableNonMovable<bsrvcore::PutGenerator>,
                      bsrvcore::PutGenerator>);
static_assert(std::is_base_of_v<
              bsrvcore::NonCopyableNonMovable<bsrvcore::MultipartGenerator>,
              bsrvcore::MultipartGenerator>);
static_assert(std::is_base_of_v<
              bsrvcore::CopyableMovable<bsrvcore::MultipartGenerator::PartSpec>,
              bsrvcore::MultipartGenerator::PartSpec>);
static_assert(std::is_base_of_v<bsrvcore::NonCopyableNonMovable<
                                    bsrvcore::AsyncTemplateWaiter<int, int>>,
                                bsrvcore::AsyncTemplateWaiter<int, int>>);
static_assert(
    std::is_base_of_v<
        bsrvcore::NonCopyableNonMovable<bsrvcore::AsyncSameTypeWaiter<int>>,
        bsrvcore::AsyncSameTypeWaiter<int>>);
static_assert(
    std::is_base_of_v<
        bsrvcore::NonCopyableNonMovable<bsrvcore::AsyncSameTypeWaiter<void>>,
        bsrvcore::AsyncSameTypeWaiter<void>>);

static_assert(std::is_copy_constructible_v<bsrvcore::HttpClientOptions>);
static_assert(std::is_move_constructible_v<bsrvcore::HttpClientOptions>);
static_assert(std::is_copy_constructible_v<bsrvcore::HttpSseClientOptions>);
static_assert(std::is_move_constructible_v<bsrvcore::HttpSseClientOptions>);
static_assert(std::is_copy_constructible_v<bsrvcore::HttpRouteResult>);
static_assert(std::is_move_constructible_v<bsrvcore::HttpRouteResult>);
static_assert(std::is_move_constructible_v<bsrvcore::BluePrint>);
static_assert(!std::is_copy_constructible_v<bsrvcore::BluePrint>);
static_assert(std::is_move_constructible_v<bsrvcore::ReuseableBluePrint>);
static_assert(!std::is_copy_constructible_v<bsrvcore::ReuseableBluePrint>);
static_assert(!std::is_copy_constructible_v<bsrvcore::AtomicSharedPtr<int>>);
static_assert(!std::is_move_constructible_v<bsrvcore::AtomicSharedPtr<int>>);
static_assert(std::is_copy_constructible_v<DummyParameterMap>);
static_assert(std::is_move_constructible_v<DummyParameterMap>);
static_assert(!std::is_copy_constructible_v<bsrvcore::FileReader>);
static_assert(!std::is_move_constructible_v<bsrvcore::FileReader>);
static_assert(!std::is_copy_constructible_v<bsrvcore::PutGenerator>);
static_assert(!std::is_move_constructible_v<bsrvcore::PutGenerator>);
static_assert(std::is_same_v<bsrvcore::HttpClientTask::Executor,
                             boost::asio::io_context::executor_type>);
static_assert(std::is_same_v<bsrvcore::HttpSseClientTask::Executor,
                             boost::asio::io_context::executor_type>);
static_assert(!std::is_convertible_v<boost::asio::thread_pool::executor_type,
                                     bsrvcore::HttpClientTask::Executor>);
static_assert(!std::is_convertible_v<boost::asio::thread_pool::executor_type,
                                     bsrvcore::HttpSseClientTask::Executor>);

}  // namespace

TEST(PublicTraitsTest, PublicBasesRemainDerivable) {
  DummyLogger logger;
  DummyParameterMap parameters;
  DummyHandler handler;
  DummyAspect aspect;
  DummyCloneableHandler cloneable_handler;
  DummyCloneableAspect cloneable_aspect;
  DummyAttribute attribute;
  DummyHandlerFactory handler_factory;
  DummyAspectFactory aspect_factory;

  EXPECT_TRUE(parameters.Get(bsrvcore::bsrvrun::String()).Empty());
  EXPECT_EQ(attribute.ToString(), attribute.Type().name());
  logger.Log(bsrvcore::LogLevel::kInfo, "ok");
  handler.Service(nullptr);
  aspect.PreService(nullptr);
  aspect.PostService(nullptr);
  EXPECT_TRUE(cloneable_handler.Clone() != nullptr);
  EXPECT_TRUE(cloneable_aspect.Clone() != nullptr);
  EXPECT_EQ(handler_factory.Create(nullptr).get(), nullptr);
  EXPECT_EQ(aspect_factory.Create(nullptr).get(), nullptr);
}
