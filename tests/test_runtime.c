#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "../console.c"
#include "../console.h"
#include "../cutils.c"
#include "../cutils.h"
#include "../headers.c"
#include "../runtime.c"
#include "../runtime.h"
#include "./file.c"

void execution_complete(void *arg) {
  char *filename = (char *)arg;
  fprintf(stderr, "[INFO] Execution of %s completed.\n", filename);
  free(filename); // 释放拷贝的 filename
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <js_file1> [<js_file2> ...] \n", argv[0]);
    return 1;
  }

  WorkerRuntime *wrt = Worker_NewRuntime(10);
  if (!wrt) {
    fprintf(stderr, "Failed to initialize worker runtime\n");
    return 1;
  }

  // JS文件数量
  int num_files = argc - 1;

  // 添加任务到队列
  for (int i = 0; i < num_files; i++) {
    const char *filename = argv[i + 1];
    char *js_code = read_file_to_string(filename);

    char *filename_copy = strdup(filename);

    Worker_Eval_JS(wrt, js_code, execution_complete, filename_copy);
    free(js_code);
  }

  clock_t start_time = clock();

  // Worker_RunLoop(wrt);
  int has_pending_events = 1;
  while (has_pending_events != 0 && (clock() - start_time < 500000)) {
    has_pending_events = Worker_RunLoopOnce(wrt);
  }

  fprintf(stderr, "finish uv loop.\n");

  Worker_FreeRuntime(wrt);

  fprintf(stderr, "test finished.\n");
}
