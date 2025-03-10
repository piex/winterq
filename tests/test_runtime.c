#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "../runtime.c"
#include "./file.c"

int main(int argc, char **argv)
{
  if (argc < 2)
  {
    fprintf(stderr, "Usage: %s <js_file1> [<js_file2> ...] \n", argv[0]);
    return 1;
  }

  WorkerRuntime *wrt = Worker_NewRuntime(10);
  if (!wrt)
  {
    fprintf(stderr, "Failed to initialize worker runtime\n");
    return 1;
  }

  // JS文件数量
  int num_files = argc - 1;

  // 添加任务到队列
  for (int i = 0; i < num_files; i++)
  {
    char *js_code = read_file_to_string(argv[i + 1]);
    Worker_Eval_JS(wrt, js_code);
    free(js_code);
  }

  Worker_RunLoop(wrt);
  fprintf(stderr, "finish uv loop\n");

  Worker_FreeRuntime(wrt);
}
