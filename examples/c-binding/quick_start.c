#include <bsrvcore-c/bsrvcore.h>

#include <stdio.h>

static void hello_handler(bsrvcore_http_server_task_t* task) {
  bsrvcore_http_server_task_set_response(
      task, 200, "text/plain; charset=utf-8", "Hello, bsrvcore C binding.\n",
      sizeof("Hello, bsrvcore C binding.\n") - 1);
}

int main(void) {
  bsrvcore_server_t* server = NULL;
  if (bsrvcore_server_create(4, &server) != BSRVCORE_RESULT_OK) {
    fprintf(stderr, "failed to create server\n");
    return 1;
  }

  if (bsrvcore_server_add_route(server, BSRVCORE_HTTP_METHOD_GET, "/hello",
                                hello_handler) != BSRVCORE_RESULT_OK ||
      bsrvcore_server_add_listen(server, "0.0.0.0", 8080, 2) !=
          BSRVCORE_RESULT_OK ||
      bsrvcore_server_start(server) != BSRVCORE_RESULT_OK) {
    fprintf(stderr, "failed to start server\n");
    bsrvcore_server_destroy(server);
    return 1;
  }

  puts("Listening on http://0.0.0.0:8080/hello");
  puts("Press Enter to stop.");
  (void)getchar();

  (void)bsrvcore_server_stop(server);
  bsrvcore_server_destroy(server);
  return 0;
}
