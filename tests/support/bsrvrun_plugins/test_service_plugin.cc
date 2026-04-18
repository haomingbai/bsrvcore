#include <fstream>
#include <string>

#include "bsrvcore/bsrvrun/parameter_map.h"
#include "bsrvcore/bsrvrun/service_factory.h"
#include "test_service_api.h"

namespace {

class TestServiceFactory : public bsrvcore::bsrvrun::ServiceFactory {
 public:
  void* GenerateService(bsrvcore::bsrvrun::ParameterMap* parameters) override {
    auto* service = new TestServiceData();
    service->body = "service|";
    if (parameters != nullptr) {
      const auto body =
          parameters->Get(bsrvcore::bsrvrun::String("body")).ToStdString();
      if (!body.empty()) {
        service->body = body;
      }
      service->destroy_marker =
          parameters->Get(bsrvcore::bsrvrun::String("destroy_marker"))
              .ToStdString();
    }
    return service;
  }

  void DestroyService(void* service) override {
    auto* typed = static_cast<TestServiceData*>(service);
    if (typed == nullptr) {
      return;
    }

    if (!typed->destroy_marker.empty()) {
      std::ofstream out(typed->destroy_marker, std::ios::trunc);
      out << typed->body;
    }

    delete typed;
  }
};

TestServiceFactory g_factory;

}  // namespace

BSRVCORE_BSRVRUN_SERVICE_FACTORY_EXPORT GetServiceFactory() {
  return &g_factory;
}
