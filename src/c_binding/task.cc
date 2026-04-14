#include "internal/common.h"

namespace cbind = bsrvcore::c_binding_internal;

#define BSRVCORE_DEFINE_TASK_API(WrapperType, NativeType, Prefix)              \
  bsrvcore_result_t Prefix##_get_request_header(                               \
      const WrapperType* task, const char* key, const char** out_value,        \
      size_t* out_value_len) {                                                 \
    return cbind::Guard([&]() {                                                \
      return cbind::GetRequestHeaderForWrapper<WrapperType, NativeType>(       \
          task, key, out_value, out_value_len);                                \
    });                                                                        \
  }                                                                            \
  bsrvcore_result_t Prefix##_get_request_body(                                 \
      const WrapperType* task, const char** out_body, size_t* out_body_len) {  \
    return cbind::Guard([&]() {                                                \
      return cbind::GetRequestBodyForWrapper<WrapperType, NativeType>(         \
          task, out_body, out_body_len);                                       \
    });                                                                        \
  }                                                                            \
  bsrvcore_result_t Prefix##_set_status(WrapperType* task, int status_code) {  \
    return cbind::Guard([&]() {                                                \
      return cbind::SetStatusForWrapper<WrapperType, NativeType>(task,         \
                                                                 status_code); \
    });                                                                        \
  }                                                                            \
  bsrvcore_result_t Prefix##_set_response_header(                              \
      WrapperType* task, const char* key, const char* value) {                 \
    return cbind::Guard([&]() {                                                \
      return cbind::SetResponseHeaderForWrapper<WrapperType, NativeType>(      \
          task, key, value);                                                   \
    });                                                                        \
  }                                                                            \
  bsrvcore_result_t Prefix##_set_response_body(WrapperType* task,              \
                                               const char* body, size_t len) { \
    return cbind::Guard([&]() {                                                \
      return cbind::SetResponseBodyForWrapper<WrapperType, NativeType>(        \
          task, body, len);                                                    \
    });                                                                        \
  }                                                                            \
  bsrvcore_result_t Prefix##_append_response_body(                             \
      WrapperType* task, const char* body, size_t len) {                       \
    return cbind::Guard([&]() {                                                \
      return cbind::AppendResponseBodyForWrapper<WrapperType, NativeType>(     \
          task, body, len);                                                    \
    });                                                                        \
  }                                                                            \
  bsrvcore_result_t Prefix##_set_response(WrapperType* task, int status_code,  \
                                          const char* content_type,            \
                                          const char* body, size_t len) {      \
    return cbind::Guard([&]() {                                                \
      return cbind::SetResponseForWrapper<WrapperType, NativeType>(            \
          task, status_code, content_type, body, len);                         \
    });                                                                        \
  }

extern "C" {

BSRVCORE_DEFINE_TASK_API(bsrvcore_http_server_task_t, bsrvcore::HttpServerTask,
                         bsrvcore_http_server_task)
BSRVCORE_DEFINE_TASK_API(bsrvcore_http_pre_server_task_t,
                         bsrvcore::HttpPreServerTask,
                         bsrvcore_http_pre_server_task)
BSRVCORE_DEFINE_TASK_API(bsrvcore_http_post_server_task_t,
                         bsrvcore::HttpPostServerTask,
                         bsrvcore_http_post_server_task)

}  // extern "C"

#undef BSRVCORE_DEFINE_TASK_API
