#include <gtest/gtest.h>

#include <string>

#include "bsrvcore/core/blue_print.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/route/http_request_method.h"

namespace {

std::string MakeDeepRoute(std::size_t depth) {
  std::string route;
  route.reserve(depth * 8);
  for (std::size_t idx = 0; idx < depth; ++idx) {
    route += "/seg";
    route += std::to_string(idx);
  }
  return route.empty() ? "/" : route;
}

}  // namespace

TEST(RouteTableTeardownTest, DestroysDeepRouteTreeIteratively) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(1);
  server->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, MakeDeepRoute(4096),
                        [](std::shared_ptr<bsrvcore::HttpServerTask>) {});

  server.reset();
  SUCCEED();
}

TEST(RouteTableTeardownTest, DestroysMountedDeepBlueprintTreeIteratively) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(1);
  auto blue_print = bsrvcore::BluePrintFactory::Create();
  blue_print.AddRouteEntry(bsrvcore::HttpRequestMethod::kGet,
                           MakeDeepRoute(4096),
                           [](std::shared_ptr<bsrvcore::HttpServerTask>) {});

  server->AddBluePrint("/mounted", std::move(blue_print));
  server.reset();
  SUCCEED();
}
