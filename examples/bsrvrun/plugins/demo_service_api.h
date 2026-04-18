/**
 * @file demo_service_api.h
 * @brief Shared example service type used by bsrvrun demo plugins.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef EXAMPLES_BSRVRUN_PLUGINS_DEMO_SERVICE_API_H_
#define EXAMPLES_BSRVRUN_PLUGINS_DEMO_SERVICE_API_H_

#include <string>

struct DemoServiceData {
  std::string prefix;
};

#endif
