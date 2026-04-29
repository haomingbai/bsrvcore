/**
 * @file bsrvcore.h
 * @brief Umbrella header for bsrvcore public API.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-04
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Include this header to access the full set of public bsrvcore types.
 * It re-exports all installed headers under `include/bsrvcore/`.
 */

#pragma once

#ifndef BSRVCORE_BSRVCORE_H_
#define BSRVCORE_BSRVCORE_H_

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/bsrvrun/http_request_aspect_handler_factory.h"
#include "bsrvcore/bsrvrun/http_request_handler_factory.h"
#include "bsrvcore/bsrvrun/logger_factory.h"
#include "bsrvcore/bsrvrun/parameter_map.h"
#include "bsrvcore/bsrvrun/service_factory.h"
#include "bsrvcore/bsrvrun/string.h"
#include "bsrvcore/connection/client/http_client_session.h"
#include "bsrvcore/connection/client/http_client_session_attribute.h"
#include "bsrvcore/connection/client/http_client_task.h"
#include "bsrvcore/connection/client/http_sse_client_task.h"
#include "bsrvcore/connection/client/multipart_generator.h"
#include "bsrvcore/connection/client/put_generator.h"
#include "bsrvcore/connection/client/request_assembler.h"
#include "bsrvcore/connection/client/sse_event_parser.h"
#include "bsrvcore/connection/client/stream_builder.h"
#include "bsrvcore/connection/client/websocket_client_task.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/connection/server/multipart_parser.h"
#include "bsrvcore/connection/server/put_processor.h"
#include "bsrvcore/connection/server/server_set_cookie.h"
#include "bsrvcore/connection/server/websocket_server_task.h"
#include "bsrvcore/connection/websocket/websocket_task_base.h"
#include "bsrvcore/core/async_waiter.h"
#include "bsrvcore/core/blue_print.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/core/logger.h"
#include "bsrvcore/core/service_provider.h"
#include "bsrvcore/core/trait.h"
#include "bsrvcore/core/types.h"
#include "bsrvcore/file/file_reader.h"
#include "bsrvcore/file/file_state.h"
#include "bsrvcore/file/file_writer.h"
#include "bsrvcore/route/cloneable_http_request_aspect_handler.h"
#include "bsrvcore/route/cloneable_http_request_handler.h"
#include "bsrvcore/route/http_request_aspect_handler.h"
#include "bsrvcore/route/http_request_handler.h"
#include "bsrvcore/route/http_request_method.h"
#include "bsrvcore/route/http_route_result.h"
#include "bsrvcore/session/attribute.h"
#include "bsrvcore/session/context.h"

#endif
