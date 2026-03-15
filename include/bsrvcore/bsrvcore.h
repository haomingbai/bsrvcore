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

#include "bsrvcore/attribute.h"
#include "bsrvcore/bsrvrun/http_request_aspect_handler_factory.h"
#include "bsrvcore/bsrvrun/http_request_handler_factory.h"
#include "bsrvcore/bsrvrun/parameter_map.h"
#include "bsrvcore/bsrvrun/string.h"
#include "bsrvcore/context.h"
#include "bsrvcore/http_request_aspect_handler.h"
#include "bsrvcore/http_request_handler.h"
#include "bsrvcore/http_request_method.h"
#include "bsrvcore/http_route_result.h"
#include "bsrvcore/http_client_task.h"
#include "bsrvcore/http_client_session.h"
#include "bsrvcore/http_client_session_attribute.h"
#include "bsrvcore/http_sse_client_task.h"
#include "bsrvcore/http_server.h"
#include "bsrvcore/http_server_task.h"
#include "bsrvcore/logger.h"
#include "bsrvcore/server_set_cookie.h"
#include "bsrvcore/sse_event_parser.h"
#include "bsrvcore/trait.h"

#endif
