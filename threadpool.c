#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>

#include "threadpool.h"
#include "runtime.h"

// Forward declarations
static void init_task_queue(TaskQueue *queue);
static void enqueue_task(TaskQueue *queue, Task *task);
static void destroy_task_queue(TaskQueue *queue);
static Task *dequeue_task(TaskQueue *queue);

// 线程数据
typedef struct ThreadData
{
  ThreadPool *pool;
  int thread_id;
  int max_contexts;
  WorkerRuntime *runtime;
} ThreadData;

// 初始化任务队列
static void init_task_queue(TaskQueue *queue)
{
  queue->head = NULL;
  queue->tail = NULL;
  queue->size = 0;
  pthread_mutex_init(&queue->mutex, NULL);
  pthread_cond_init(&queue->not_empty, NULL);
}

/**
 * 向任务队列中添加任务
 *
 * @param queue 任务队列
 * @param task 要添加的任务
 */
static void enqueue_task(TaskQueue *queue, Task *task)
{
  TaskNode *node = calloc(1, sizeof(TaskNode));
  node->task = task;
  node->next = NULL;

  pthread_mutex_lock(&queue->mutex);

  if (queue->size == 0)
  {
    queue->head = node;
    queue->tail = node;
  }
  else
  {
    queue->tail->next = node;
    queue->tail = node;
  }

  queue->size++;

  // 通知等待中的线程有新任务
  pthread_cond_signal(&queue->not_empty);
  pthread_mutex_unlock(&queue->mutex);
}

/**
 * 销毁任务队列
 *
 * @param queue 要销毁的任务队列
 */
static void destroy_task_queue(TaskQueue *queue)
{
  pthread_mutex_lock(&queue->mutex);

  TaskNode *current = queue->head;
  while (current != NULL)
  {
    TaskNode *next = current->next;
    if (current->task->is_script && current->task->script != NULL)
    {
      free((void *)current->task->script); // 释放脚本字符串
    }
    else if (!current->task->is_script && current->task->bytecode != NULL)
    {
      free(current->task->bytecode); // 释放字节码
    }
    free(current);
    current = next;
  }

  queue->head = NULL;
  queue->tail = NULL;
  queue->size = 0;

  pthread_mutex_unlock(&queue->mutex);
  pthread_mutex_destroy(&queue->mutex);
  pthread_cond_destroy(&queue->not_empty);
}

/**
 * 从任务队列中获取任务
 *
 * @param queue 任务队列
 * @return 返回任务，如果队列为空则阻塞等待
 */
static Task *dequeue_task(TaskQueue *queue)
{
  pthread_mutex_lock(&queue->mutex);
  // 当队列为空时，等待任务到达
  while (queue->size == 0)
  {
    pthread_cond_wait(&queue->not_empty, &queue->mutex);
  }

  TaskNode *node = queue->head;

  if (queue->size == 1)
  {
    queue->head = NULL;
    queue->tail = NULL;
  }
  else
  {
    queue->head = queue->head->next;
  }

  queue->size--;

  pthread_mutex_unlock(&queue->mutex);
  return node->task;
}

// 执行任务
static void execute_task(WorkerRuntime *runtime, Task *task)
{
  clock_t start, end;
  start = clock();

  if (task->is_script)
  {
    // 执行JavaScript脚本
    Worker_Eval_JS(runtime, task->script);
    if (task->script != NULL)
    {
      free((void *)task->script); // 释放脚本字符串
    }
  }
  else
  { // 执行JavaScript字节码
    Worker_Eval_Bytecode(runtime, task->bytecode, task->bytecode_len);
    if (task->bytecode != NULL)
    {
      free(task->bytecode); // 释放字节码
    }
  }

  end = clock();
  task->execution_time = ((double)(end - start)) / CLOCKS_PER_SEC;
}

// 线程工作函数
static void *worker_thread(void *arg)
{

  ThreadData *thread_data = (ThreadData *)arg;
  ThreadPool *pool = thread_data->pool;

  WorkerRuntime *wrt = Worker_NewRuntime(thread_data->max_contexts);

  // 设置定时器，每1ms执行一次 Worker_RunLoopOnce
  struct timespec sleep_time;
  sleep_time.tv_sec = 0;
  sleep_time.tv_nsec = 1000000; // 1毫秒 = 1000000纳秒

  // 循环处理任务
  while (1)
  {
    pthread_mutex_lock(&pool->pool_mutex);
    bool should_exit = pool->shutdown;
    pthread_mutex_unlock(&pool->pool_mutex);
    printf("\n---should_exit-%d--\n", should_exit);

    if (should_exit)
      break;

    // 尝试获取一个任务
    Task *task = dequeue_task(&pool->queue);

    // 执行任务
    if (task != NULL)
    {
      execute_task(wrt, task);
    }

    // 每1ms运行一次事件循环
    Worker_RunLoopOnce(wrt);
    printf("\n-----------------Worker_RunLoopOnce--%d-\n", thread_data->thread_id);

    // 短暂sleep，以便其他线程可以获取任务
    nanosleep(&sleep_time, NULL);
  }

  // 清理 JSRuntime
  Worker_FreeRuntime(wrt);
  printf("Thread %d shutting down\n", thread_data->thread_id);
  return NULL;
}

/**
 * 初始化线程池
 *
 * @param num_threads 线程数量
 * @param max_contexts 每个运行时的最大上下文数
 * @return 成功返回0，失败返回非零值
 */
ThreadPool *init_thread_pool(int thread_count, int max_contexts)
{
  ThreadPool *pool = (ThreadPool *)malloc(sizeof(ThreadPool));
  if (pool == NULL)
  {
    return NULL;
  }

  pool->thread_count = thread_count;
  pool->shutdown = false;

  // 初始化任务队列
  init_task_queue(&pool->queue);

  // 创建线程数据
  ThreadData *thread_data =
      (ThreadData *)malloc(thread_count * sizeof(ThreadData));
  // 初始化互斥锁
  pthread_mutex_init(&pool->pool_mutex, NULL);

  pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * thread_count);

  // 创建工作线程
  for (int i = 0; i < thread_count; i++)
  {
    thread_data[i].pool = pool;
    thread_data[i].thread_id = i;
    thread_data[i].max_contexts = max_contexts;
    thread_data[i].runtime = NULL;
    if (pthread_create(&pool->threads[i], NULL, worker_thread, thread_data) != 0)
    {
      free(thread_data);
      return NULL;
    }
  }

  return pool;
}

/**
 * 提交JavaScript脚本任务到线程池
 *
 * @param script JavaScript脚本代码
 * @return 成功返回0，失败返回非零值
 */
int add_script_task_to_pool(ThreadPool *pool, const char *script,
                            void (*callback)(void *), void *callback_arg)
{
  static int task_id = 1;

  // 创建新任务
  Task *task = (Task *)malloc(sizeof(Task));
  if (task == NULL)
  {
    return -1;
  }

  // 复制脚本字符串
  char *script_copy = strdup(script);
  if (script_copy == NULL)
  {
    free(task);
    return -1;
  }

  task->is_script = true;
  task->script = script_copy;
  task->bytecode = NULL;
  task->bytecode_len = 0;
  task->execution_time = 0.0;
  task->task_id = task_id++;
  task->callback = callback;
  task->callback_arg = callback_arg;
  enqueue_task(&pool->queue, task);

  return 0;
}

// 关闭线程池
void shutdown_thread_pool(ThreadPool *pool)
{
  if (!pool)
    return;

  // 设置关闭标志
  pthread_mutex_lock(&pool->pool_mutex);
  pool->shutdown = true;
  pthread_mutex_unlock(&pool->pool_mutex);

  // 等待所有线程结束
  for (int i = 0; i < pool->thread_count; i++)
  {
    pthread_cond_signal(&pool->queue.not_empty);
  }

  // 等待所有线程退出
  for (int i = 0; i < pool->thread_count; i++)
  {
    pthread_join(pool->threads[i], NULL);
  }

  // 释放资源
  free(pool->threads);
  destroy_task_queue(&pool->queue);
  pthread_mutex_destroy(&pool->pool_mutex);

  free(pool);
}
