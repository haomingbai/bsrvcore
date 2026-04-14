#include <bsrvcore-c/bsrvcore.h>

int main(void) {
  return BSRVCORE_RESULT_OK == 0 && BSRVCORE_HTTP_METHOD_GET == 0 ? 0 : 1;
}
