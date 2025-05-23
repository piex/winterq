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
  printf("A task completed.\n");
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

  // JS文件数量
  int num_files = argc - 2;
  // 任务数，总文件数乘以执行次数
  int total_tasks = num_files * iterations;

  // 创建线程池（线程数等于处理器核心数）
  int num_cores = sysconf(_SC_NPROCESSORS_ONLN);

  printf("Creating thread pool with %d threads\n", num_cores);

  // 创建线程池配置
  ThreadPoolConfig config = {
      .thread_count = num_cores,
      .max_contexts = 10,
      .global_queue_size = 100,
      .local_queue_size = 10,
      .enable_work_stealing = false,
      .idle_threshold = 2,
      .dynamic_sizing = false,
  };

  // 初始化线程池
  ThreadPool *pool = init_thread_pool(config);

  if (!pool)
  {
    fprintf(stderr, "Failed to initialize thread pool\n");
    return 1;
  }

  printf("\n-------- Created thread pool successfully --------\n\n\n");

  // 添加任务到队列
  for (int i = 0; i < num_files; i++)
  {
    const char *js_code = read_file_to_string(argv[i + 1]);

    for (int j = 0; j < iterations; j++)
    {
      add_script_task_to_pool(pool, js_code, task_callback, NULL);
    }

    free((void *)js_code);
  }

  printf("Added %d tasks to the queue\n", total_tasks);

  // 等待所有任务完成（最多等待5秒）
  printf("Waiting for tasks to complete...\n");
  int wait_result = wait_for_idle(pool, 5000);

  if (wait_result == 0)
  {
    printf("All tasks completed successfully.\n");
  }
  else if (wait_result == 1)
  {
    printf("Timeout waiting for tasks to complete.\n");
  }
  else
  {
    printf("Error waiting for tasks to complete.\n");
  }

  // 获取线程池统计信息
  ThreadPoolStats stats = get_thread_pool_stats(pool);

  printf("\n================= Thread Pool Statistics =================\n");
  printf("| %-20s | %-10d |\n", "Active threads", stats.active_threads);
  printf("| %-20s | %-10d |\n", "Idle threads", stats.idle_threads);
  printf("| %-20s | \033[1;32m%-10d\033[0m |\n", "Completed tasks", stats.completed_tasks);
  printf("| %-20s | \033[1;34m%-9.2f%%\033[0m |\n", "Thread utilization", stats.thread_utilization);
  printf("===========================================================\n\n");

  // 关闭线程池
  shutdown_thread_pool(pool);

  return 0;
}
