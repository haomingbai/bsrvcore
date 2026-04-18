#pragma once

#ifndef TESTS_SUPPORT_BSRVRUN_PLUGINS_TEST_SERVICE_API_H_
#define TESTS_SUPPORT_BSRVRUN_PLUGINS_TEST_SERVICE_API_H_

#include <string>

struct TestServiceData {
  std::string body;
  std::string destroy_marker;
};

#endif
