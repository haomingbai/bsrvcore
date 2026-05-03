/**
 * @file demo_service_plugin.cc
 * @brief Minimal bsrvrun service plugin used by examples.
 */

#include <string>

#include "bsrvcore/bsrvrun/parameter_map.h"
#include "bsrvcore/bsrvrun/plugin_export.h"
#include "bsrvcore/bsrvrun/service_factory.h"
#include "bsrvcore/bsrvrun/string.h"
#include "demo_service_api.h"

namespace {

class DemoServiceFactory : public bsrvcore::bsrvrun::ServiceFactory {
 public:
  void* GenerateService(bsrvcore::bsrvrun::ParameterMap* params) override {
    auto* service = new DemoServiceData();
    service->prefix = "demo-service|";
    if (params != nullptr) {
      const auto prefix =
          params->Get(bsrvcore::bsrvrun::String("prefix")).ToStdString();
      if (!prefix.empty()) {
        service->prefix = prefix;
      }
    }
    return service;
  }

  void DestroyService(void* service) override {
    delete static_cast<DemoServiceData*>(service);
  }
};

DemoServiceFactory g_factory;

}  // namespace

BSRVCORE_BSRVRUN_SERVICE_FACTORY_EXPORT GetServiceFactory() {
  return &g_factory;
}
