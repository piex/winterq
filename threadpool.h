#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <uv.h>
#include "quickjs.h"

typedef struct
{
  int task_id;
  const char *js_code;
  double execution_time;
  void (*callback)(void *);
  void *callback_arg;
} Task;

// 任务队列节点
typedef struct TaskNode
{
  Task task;
  struct TaskNode *next;
} TaskNode;

// 任务队列
typedef struct
{
  TaskNode *head;
  TaskNode *tail;
  int size;
  uv_mutex_t mutex;
  uv_cond_t not_empty;
} TaskQueue;

typedef struct
{
  int task_id;
  double execution_time;
} TaskExecutionTime;

typedef struct
{
  uv_thread_t *threads;
  int thread_count; // 线程数
  int shutdown;
  int completed_tasks; // 已执行任务
  int max_tasks;
  uv_mutex_t completed_mutex;
  uv_mutex_t shutdown_mutex;
  uv_async_t task_complete_async;
  uv_cond_t all_completed;

  TaskQueue queue;
  TaskExecutionTime *task_execution_times;
} ThreadPool;

// API declarations
ThreadPool *init_thread_pool(int thread_count);
void add_task_to_pool(ThreadPool *pool, const char *filename, void (*callback)(void *), void *callback_arg);
void shutdown_thread_pool(ThreadPool *pool);

#endif // THREADPOOL_H
