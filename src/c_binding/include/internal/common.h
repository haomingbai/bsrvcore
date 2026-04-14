#pragma once

#ifndef BSRVCORE_C_BINDING_INTERNAL_COMMON_H_
#define BSRVCORE_C_BINDING_INTERNAL_COMMON_H_

#include <boost/asio/ip/address.hpp>
#include <boost/beast/core/string.hpp>
#include <boost/beast/http/field.hpp>
#include <memory>
#include <new>
#include <string>
#include <string_view>

#include "bsrvcore-c/bsrvcore.h"
#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/route/http_request_aspect_handler.h"
#include "bsrvcore/route/http_request_handler.h"

struct bsrvcore_server {
  bsrvcore::OwnedPtr<bsrvcore::HttpServer> native;
};

struct bsrvcore_http_server_task {
  bsrvcore::HttpServerTask* native;
};

struct bsrvcore_http_pre_server_task {
  bsrvcore::HttpPreServerTask* native;
};

struct bsrvcore_http_post_server_task {
  bsrvcore::HttpPostServerTask* native;
};

namespace bsrvcore::c_binding_internal {

template <typename Fn>
bsrvcore_result_t Guard(Fn&& fn) noexcept {
  try {
    return fn();
  } catch (const std::bad_alloc&) {
    return BSRVCORE_RESULT_ALLOCATION_FAILURE;
  } catch (...) {
    return BSRVCORE_RESULT_INTERNAL_ERROR;
  }
}

template <typename Task>
void SetInternalErrorResponse(Task* task) noexcept {
  if (task == nullptr) {
    return;
  }
  try {
    task->GetResponse().result(bsrvcore::HttpStatus::internal_server_error);
    task->SetField(bsrvcore::HttpField::content_type,
                   "text/plain; charset=utf-8");
    task->SetBody("Unhandled exception in bsrvcore C binding callback.");
  } catch (...) {
  }
}

inline bool TryConvertMethod(bsrvcore_http_method_t method,
                             bsrvcore::HttpRequestMethod* out_method) noexcept {
  if (out_method == nullptr) {
    return false;
  }

  switch (method) {
    case BSRVCORE_HTTP_METHOD_GET:
      *out_method = bsrvcore::HttpRequestMethod::kGet;
      return true;
    case BSRVCORE_HTTP_METHOD_POST:
      *out_method = bsrvcore::HttpRequestMethod::kPost;
      return true;
    case BSRVCORE_HTTP_METHOD_PUT:
      *out_method = bsrvcore::HttpRequestMethod::kPut;
      return true;
    case BSRVCORE_HTTP_METHOD_DELETE:
      *out_method = bsrvcore::HttpRequestMethod::kDelete;
      return true;
    case BSRVCORE_HTTP_METHOD_PATCH:
      *out_method = bsrvcore::HttpRequestMethod::kPatch;
      return true;
    case BSRVCORE_HTTP_METHOD_HEAD:
      *out_method = bsrvcore::HttpRequestMethod::kHead;
      return true;
  }

  return false;
}

inline bsrvcore_result_t ValidateServer(
    const bsrvcore_server_t* server) noexcept {
  return (server == nullptr || !server->native)
             ? BSRVCORE_RESULT_INVALID_ARGUMENT
             : BSRVCORE_RESULT_OK;
}

template <typename TaskWrapper>
inline bsrvcore_result_t ValidateTask(const TaskWrapper* task) noexcept {
  return (task == nullptr || task->native == nullptr)
             ? BSRVCORE_RESULT_INVALID_ARGUMENT
             : BSRVCORE_RESULT_OK;
}

inline bsrvcore_result_t ValidateStringArg(const char* value) noexcept {
  return value == nullptr ? BSRVCORE_RESULT_INVALID_ARGUMENT
                          : BSRVCORE_RESULT_OK;
}

inline bsrvcore_result_t ValidateBufferArg(const char* data,
                                           size_t len) noexcept {
  return (data == nullptr && len != 0) ? BSRVCORE_RESULT_INVALID_ARGUMENT
                                       : BSRVCORE_RESULT_OK;
}

inline std::string CopyBuffer(const char* data, size_t len) {
  if (data == nullptr || len == 0) {
    return {};
  }
  return std::string(data, len);
}

template <typename Task>
bsrvcore_result_t GetRequestHeaderImpl(Task* task, const char* key,
                                       const char** out_value,
                                       size_t* out_value_len) {
  if (task == nullptr || key == nullptr || out_value == nullptr) {
    return BSRVCORE_RESULT_INVALID_ARGUMENT;
  }

  const auto& req = task->GetRequest();
  const auto it = req.find(boost::beast::string_view(key));
  if (it == req.end()) {
    return BSRVCORE_RESULT_NOT_FOUND;
  }

  const auto value = it->value();
  *out_value = value.data();
  if (out_value_len != nullptr) {
    *out_value_len = value.size();
  }
  return BSRVCORE_RESULT_OK;
}

template <typename Task>
bsrvcore_result_t GetRequestBodyImpl(Task* task, const char** out_body,
                                     size_t* out_body_len) {
  if (task == nullptr || out_body == nullptr) {
    return BSRVCORE_RESULT_INVALID_ARGUMENT;
  }

  const auto& body = task->GetRequest().body();
  *out_body = body.data();
  if (out_body_len != nullptr) {
    *out_body_len = body.size();
  }
  return BSRVCORE_RESULT_OK;
}

template <typename Task>
bsrvcore_result_t SetStatusImpl(Task* task, int status_code) {
  if (task == nullptr) {
    return BSRVCORE_RESULT_INVALID_ARGUMENT;
  }

  task->GetResponse().result(static_cast<bsrvcore::HttpStatus>(status_code));
  return BSRVCORE_RESULT_OK;
}

template <typename Task>
bsrvcore_result_t SetResponseHeaderImpl(Task* task, const char* key,
                                        const char* value) {
  if (task == nullptr || key == nullptr || value == nullptr) {
    return BSRVCORE_RESULT_INVALID_ARGUMENT;
  }

  task->SetField(key, value);
  return BSRVCORE_RESULT_OK;
}

template <typename Task>
bsrvcore_result_t SetResponseBodyImpl(Task* task, const char* body,
                                      size_t len) {
  if (task == nullptr) {
    return BSRVCORE_RESULT_INVALID_ARGUMENT;
  }
  if (ValidateBufferArg(body, len) != BSRVCORE_RESULT_OK) {
    return BSRVCORE_RESULT_INVALID_ARGUMENT;
  }

  task->SetBody(CopyBuffer(body, len));
  return BSRVCORE_RESULT_OK;
}

template <typename Task>
bsrvcore_result_t AppendResponseBodyImpl(Task* task, const char* body,
                                         size_t len) {
  if (task == nullptr) {
    return BSRVCORE_RESULT_INVALID_ARGUMENT;
  }
  if (ValidateBufferArg(body, len) != BSRVCORE_RESULT_OK) {
    return BSRVCORE_RESULT_INVALID_ARGUMENT;
  }

  task->AppendBody(body == nullptr ? std::string_view{}
                                   : std::string_view(body, len));
  return BSRVCORE_RESULT_OK;
}

template <typename Task>
bsrvcore_result_t SetResponseImpl(Task* task, int status_code,
                                  const char* content_type, const char* body,
                                  size_t len) {
  if (task == nullptr) {
    return BSRVCORE_RESULT_INVALID_ARGUMENT;
  }
  if (ValidateBufferArg(body, len) != BSRVCORE_RESULT_OK) {
    return BSRVCORE_RESULT_INVALID_ARGUMENT;
  }

  task->GetResponse().result(static_cast<bsrvcore::HttpStatus>(status_code));
  if (content_type != nullptr) {
    task->SetField(bsrvcore::HttpField::content_type, content_type);
  }
  task->SetBody(CopyBuffer(body, len));
  return BSRVCORE_RESULT_OK;
}

template <typename TaskWrapper, typename NativeTask>
bsrvcore_result_t GetRequestHeaderForWrapper(const TaskWrapper* task,
                                             const char* key,
                                             const char** out_value,
                                             size_t* out_value_len) {
  if (ValidateTask(task) != BSRVCORE_RESULT_OK) {
    return BSRVCORE_RESULT_INVALID_ARGUMENT;
  }
  return GetRequestHeaderImpl<NativeTask>(task->native, key, out_value,
                                          out_value_len);
}

template <typename TaskWrapper, typename NativeTask>
bsrvcore_result_t GetRequestBodyForWrapper(const TaskWrapper* task,
                                           const char** out_body,
                                           size_t* out_body_len) {
  if (ValidateTask(task) != BSRVCORE_RESULT_OK) {
    return BSRVCORE_RESULT_INVALID_ARGUMENT;
  }
  return GetRequestBodyImpl<NativeTask>(task->native, out_body, out_body_len);
}

template <typename TaskWrapper, typename NativeTask>
bsrvcore_result_t SetStatusForWrapper(TaskWrapper* task, int status_code) {
  if (ValidateTask(task) != BSRVCORE_RESULT_OK) {
    return BSRVCORE_RESULT_INVALID_ARGUMENT;
  }
  return SetStatusImpl<NativeTask>(task->native, status_code);
}

template <typename TaskWrapper, typename NativeTask>
bsrvcore_result_t SetResponseHeaderForWrapper(TaskWrapper* task,
                                              const char* key,
                                              const char* value) {
  if (ValidateTask(task) != BSRVCORE_RESULT_OK) {
    return BSRVCORE_RESULT_INVALID_ARGUMENT;
  }
  return SetResponseHeaderImpl<NativeTask>(task->native, key, value);
}

template <typename TaskWrapper, typename NativeTask>
bsrvcore_result_t SetResponseBodyForWrapper(TaskWrapper* task, const char* body,
                                            size_t len) {
  if (ValidateTask(task) != BSRVCORE_RESULT_OK) {
    return BSRVCORE_RESULT_INVALID_ARGUMENT;
  }
  return SetResponseBodyImpl<NativeTask>(task->native, body, len);
}

template <typename TaskWrapper, typename NativeTask>
bsrvcore_result_t AppendResponseBodyForWrapper(TaskWrapper* task,
                                               const char* body, size_t len) {
  if (ValidateTask(task) != BSRVCORE_RESULT_OK) {
    return BSRVCORE_RESULT_INVALID_ARGUMENT;
  }
  return AppendResponseBodyImpl<NativeTask>(task->native, body, len);
}

template <typename TaskWrapper, typename NativeTask>
bsrvcore_result_t SetResponseForWrapper(TaskWrapper* task, int status_code,
                                        const char* content_type,
                                        const char* body, size_t len) {
  if (ValidateTask(task) != BSRVCORE_RESULT_OK) {
    return BSRVCORE_RESULT_INVALID_ARGUMENT;
  }
  return SetResponseImpl<NativeTask>(task->native, status_code, content_type,
                                     body, len);
}

}  // namespace bsrvcore::c_binding_internal

#endif  // BSRVCORE_C_BINDING_INTERNAL_COMMON_H_
