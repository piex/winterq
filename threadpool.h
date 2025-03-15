#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <stdbool.h>
#include <pthread.h>

#include "runtime.h"

typedef struct Task
{
  int task_id;
  const char *script; // js script

  uint8_t *bytecode;   // 或者 JavaScript 字节码
  size_t bytecode_len; // 字节码长度

  int is_script; // 标识是脚本还是字节码

  double execution_time;
  void (*callback)(void *);
  void *callback_arg;
} Task;

// 任务队列节点
typedef struct TaskNode
{
  Task *task;
  struct TaskNode *next;
} TaskNode;

// 任务队列
typedef struct TaskQueue
{
  TaskNode *head;           // 队列头
  TaskNode *tail;           // 队列尾
  int size;                 // 队列中任务数量
  pthread_mutex_t mutex;    // 队列互斥锁
  pthread_cond_t not_empty; // 队列非空条件变量
} TaskQueue;

typedef struct
{
  pthread_t *threads; // 线程数组
  int thread_count;   // 线程数
  bool shutdown;      // 关闭标志

  int max_tasks;
  int completed_tasks; // 已执行任务

  pthread_mutex_t pool_mutex; // 线程池互斥锁

  TaskQueue queue;
} ThreadPool;

// API declarations
ThreadPool *init_thread_pool(int thread_count, int max_contexts);
int add_script_task_to_pool(ThreadPool *pool, const char *filename, void (*callback)(void *), void *callback_arg);
void shutdown_thread_pool(ThreadPool *pool);

#endif // THREADPOOL_H
