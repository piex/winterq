#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "../threadpool.h"
#include "../threadpool.c"
#include "../eventloop.h"
#include "../eventloop.c"
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

  init_loop();

  clock_t start,
      end;
  start = clock();

  // JS文件数量
  int num_files = argc - 2;
  // 任务数，总文件数乘以执行次数
  int total_tasks = num_files * iterations;

  // 创建线程池（线程数等于处理器核心数）
  int num_cores = sysconf(_SC_NPROCESSORS_ONLN);

  printf("Creating thread pool with %d threads for %d tasks\n", num_cores, total_tasks);
  // 初始化线程池
  ThreadPool *pool = init_thread_pool(num_cores);

  if (!pool)
  {
    fprintf(stderr, "Failed to initialize thread pool\n");
    return 1;
  }

  // 添加任务到队列
  for (int i = 0; i < num_files; i++)
  {
    const char *js_code = read_file_to_string(argv[i + 1]);

    for (int j = 0; j < iterations; j++)
    {
      add_task_to_pool(pool, js_code, task_callback, NULL);
    }
  }

  printf("Added %d tasks to the queue\n", total_tasks);

  // 等待所有任务完成
  uv_mutex_lock(&pool->completed_mutex);

  while (pool->completed_tasks < total_tasks)
  {
    uv_cond_wait(&pool->all_completed, &pool->completed_mutex);
  }
  uv_mutex_unlock(&pool->completed_mutex);

  // 打印结果
  printf("\nExecution Results:\n");
  printf("--------------------------------------------------\n");
  printf("%-20s | %-15s\n", "File", "Time (seconds)");
  printf("--------------------------------------------------\n");

  double *file_times = (double *)calloc(num_files, sizeof(double));
  // 累加每个文件的执行时间和迭代次数
  for (int i = 0; i < total_tasks; i++)
  {
    // 找出当前任务对应的文件索引
    int file_index = -1;
    for (int j = 0; j < num_files; j++)
    {
      if (strcmp(argv[i], argv[j + 1]) == 0)
      {
        file_index = j;
        break;
      }
    }

    if (file_index >= 0)
    {
      file_times[file_index] += pool->task_execution_times[i].execution_time;
    }
  }

  double total_time = 0.0;
  for (int i = 0; i < num_files; i++)
  {
    printf("%-20s | %-15.6f\n", argv[i + 1], file_times[i]);
    total_time += file_times[i];
  }

  printf("--------------------------------------------------\n");
  printf("Total execution time across all tasks: %.6f seconds.\n", total_time);
  printf("Average execution time per task: %.6f ms.\n", total_time / total_tasks * 1000);

  free(file_times);

  // 关闭线程池
  shutdown_thread_pool(pool);

  uv_loop_close(loop);

  // 清理资源
  end = clock();
  printf("Total execution time: %.6f seconds.\n", ((double)(end - start)) / CLOCKS_PER_SEC);

  return 0;
}
