#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "../threadpool.h"
#include "../threadpool.c"
#include "../runtime.c"
#include "../console.c"
#include "./file.c"

// 回调函数示例
void task_callback(void *arg)
{
  Task *task = (Task *)arg;
  printf("Task %d completed in %.6f seconds\n", task->task_id, task->execution_time);
}

int main(int argc, char **argv)
{
  if (argc < 3)
  {
    fprintf(stderr, "Usage: %s <js_file1> [<js_file2> ...] <iterations>\n", argv[0]);
    return 1;
  }

  // 解析执行次数（最后一个参数）
  int iterations = atoi(argv[argc - 1]);
  if (iterations <= 0)
  {
    fprintf(stderr, "Invalid number of iterations: %s\n", argv[argc - 1]);
    return 1;
  }

  clock_t start, end;
  start = clock();

  // JS文件数量
  int num_files = argc - 2;
  // 任务数，总文件数乘以执行次数
  int total_tasks = num_files * iterations;

  // 创建线程池（线程数等于处理器核心数）
  int num_cores = sysconf(_SC_NPROCESSORS_ONLN);

  printf("Creating thread pool with %d threads\n", num_cores);
  // 初始化线程池
  ThreadPool *pool = init_thread_pool(num_cores, 100);

  if (!pool)
  {
    fprintf(stderr, "Failed to initialize thread pool\n");
    return 1;
  }

  printf("Created thread pool success.\n");

  // 添加任务到队列
  for (int i = 0; i < num_files; i++)
  {
    const char *js_code = read_file_to_string(argv[i + 1]);

    for (int j = 0; j < iterations; j++)
    {
      add_script_task_to_pool(pool, js_code, task_callback, NULL);
    }
  }

  printf("Added %d tasks to the queue\n", total_tasks);

  sleep(10);

  // 清理资源
  end = clock();
  printf("Total execution time: %.6f seconds.\n", ((double)(end - start)) / CLOCKS_PER_SEC);

  return 0;
}
