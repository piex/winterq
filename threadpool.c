/**
 * @file threadpool.c
 * @brief 高性能线程池实现，支持工作窃取、动态调整、优先级调度等特性
 *
 * 这个线程池实现专为高并发JavaScript任务执行设计，核心特性包括：
 * - 全局任务队列与线程本地队列双重队列架构
 * - 工作窃取算法实现负载均衡
 * - 可配置的队列大小限制和背压机制
 * - 线程状态跟踪和统计收集
 * - 动态线程池大小调整
 * - 优先级任务调度
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>

#include "log.h"
#include "threadpool.h"
#include "runtime.h"

// Forward declarations
static void init_task_queue(TaskQueue *queue, size_t max_size);
static void task_completion_callback(void *arg);
static int enqueue_task(TaskQueue *queue, Task *task);
static void destroy_task_queue(TaskQueue *queue);
static Task *dequeue_task(TaskQueue *queue);
static Task *steal_task(ThreadPool *pool, int thief_id);
static void *worker_thread(void *arg);
static void *pool_adjuster_thread(void *arg);
static int create_worker_thread(ThreadPool *pool, int thread_id);
static uint64_t get_current_time_ms(void);
static void execute_task(WorkerRuntime *runtime, Task *task);

/**
 * @brief 获取当前时间戳（毫秒）
 * @return 当前时间的毫秒时间戳
 */
static uint64_t get_current_time_ms(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)(tv.tv_sec) * 1000 + (uint64_t)(tv.tv_usec) / 1000;
}

// 回调包装函数，用于计算执行时间并释放任务
static void task_completion_callback(void *arg)
{
  Task *task = (Task *)arg;
  if (!task)
    return;

  // 获取线程池指针（需要在Task结构体中添加）
  ThreadPool *pool = task->pool;
  if (!pool)
  {
    // 如果没有线程池指针，只释放任务
    free(task);
    return;
  }

  // 计算执行时间
  clock_t end_time = clock();
  task->execution_time = ((double)(end_time - task->start_time)) / CLOCKS_PER_SEC;

  WINTERQ_LOG_DEBUG("Task %d executed in %.2f seconds\n",
                    task->task_id, task->execution_time);

  // 更新完成任务计数
  atomic_fetch_add(&pool->completed_tasks, 1);

  // 保存回调信息，因为我们将在释放 wctx 之前调用它
  void (*callback)(void *) = task->callback;
  void *callback_arg = task->callback_arg;

  // 释放任务
  free(task);

  // 调用原始回调（如果有）
  if (callback)
  {
    callback(callback_arg);
  }

  // 通知等待线程
  pthread_mutex_lock(&pool->wait_mutex);
  pthread_cond_signal(&pool->wait_cond);
  pthread_mutex_unlock(&pool->wait_mutex);
}

/**
 * @brief 初始化任务队列
 * @param queue 要初始化的队列
 * @param max_size 队列最大容量，0表示无限制
 */
static void init_task_queue(TaskQueue *queue, size_t max_size)
{
  if (queue == NULL)
    return;

  queue->head = NULL;
  queue->tail = NULL;
  queue->size = 0;
  queue->max_size = max_size;
  // 初始化同步原语
  if (pthread_mutex_init(&queue->mutex, NULL) != 0)
  {
    WINTERQ_LOG_ERROR("Failed to initialize queue mutex\n");
    return;
  }

  if (pthread_cond_init(&queue->not_empty, NULL) != 0)
  {
    WINTERQ_LOG_ERROR("Failed to initialize queue not_empty condition\n");
    pthread_mutex_destroy(&queue->mutex);
    return;
  }

  if (pthread_cond_init(&queue->not_full, NULL) != 0)
  {
    WINTERQ_LOG_ERROR("Failed to initialize queue not_full condition\n");
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->not_empty);
    return;
  }

  WINTERQ_LOG_INFO("Task queue initialized with max size: %zu\n", max_size);
}

/**
 * @brief 销毁任务队列并释放所有资源
 * @param queue 要销毁的队列
 */
static void destroy_task_queue(TaskQueue *queue)
{
  if (queue == NULL)
    return;

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

  // 销毁同步原语
  pthread_mutex_destroy(&queue->mutex);
  pthread_cond_destroy(&queue->not_empty);
  pthread_cond_destroy(&queue->not_full);

  WINTERQ_LOG_INFO("Task queue destroyed\n");
}

/**
 * @brief 向任务队列中添加任务
 * @param queue 目标队列
 * @param task 要添加的任务
 * @return 成功返回0，队列满返回1，其他错误返回-1
 */
static int enqueue_task(TaskQueue *queue, Task *task)
{
  if (queue == NULL || task == NULL)
    return -1;

  TaskNode *node;

  pthread_mutex_lock(&queue->mutex);

  // Check if queue is full
  while (queue->max_size > 0 && queue->size >= queue->max_size)
  {
    // Wait for space with timeout
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += 100 * 1000000; // 100 millisecond timeout

    int wait_status = pthread_cond_timedwait(&queue->not_full, &queue->mutex, &ts);
    if (wait_status == ETIMEDOUT)
    {
      pthread_mutex_unlock(&queue->mutex);
      return 1; // Queue is full, couldn't add task
    }
  }

  node = calloc(1, sizeof(TaskNode));
  if (node == NULL)
  { // 内存分配失败
    pthread_mutex_unlock(&queue->mutex);
    return -1;
  }

  node->task = task;
  node->next = NULL;

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

  return 0;
}

/**
 * @brief 从任务队列中取出任务
 * @param queue 源队列
 * @return 任务指针，如果队列为空返回NULL
 */
static Task *dequeue_task(TaskQueue *queue)
{
  if (queue == NULL)
    return NULL;

  Task *task = NULL;
  TaskNode *node;

  pthread_mutex_lock(&queue->mutex);

  // 如果队列为空，使用条件变量等待而不是立即返回
  if (queue->size == 0)
  {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += 10 * 1000000; // 10ms timeout
    if (ts.tv_nsec >= 1000 * 1000000)
    {
      ts.tv_sec += 1;
      ts.tv_nsec -= 1000000000;
    }

    int wait_status = pthread_cond_timedwait(&queue->not_empty, &queue->mutex, &ts);
    if (wait_status != 0 || queue->size == 0)
    {
      pthread_mutex_unlock(&queue->mutex);
      return NULL;
    }
  }

  node = queue->head;

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

  // Signal that queue is not full
  if (queue->max_size > 0 && queue->size < queue->max_size)
  { // 通知等待的生产者线程
    pthread_cond_signal(&queue->not_full);
  }

  pthread_mutex_unlock(&queue->mutex);

  if (node != NULL)
  {
    task = node->task;
    free(node);
  }

  return task;
}

/**
 * Try to steal a task from another thread
 *
 * @param pool The thread pool
 * @param thief_id ID of the thread attempting to steal
 * @return A stolen task or NULL if no task could be stolen
 */
static Task *steal_task(ThreadPool *pool, int thief_id)
{
  if (pool == NULL || thief_id < 0 || thief_id >= pool->thread_count)
    return NULL;

  Task *task = NULL;
  ThreadData *thread_data = pool->thread_data;

  // 随机选择起始点以避免窃取模式
  int start_victim = rand() % pool->thread_count;

  // Try to steal from other threads
  for (int i = 0; i < pool->thread_count; i++)
  {
    int victim_id = (start_victim + i) % pool->thread_count;
    // Don't steal from yourself
    if (victim_id == thief_id)
      continue;

    // 如果目标线程是空闲的，可能没有任务可偷
    if (atomic_load(&thread_data[victim_id].idle))
    {
      continue;
    }

    // Try to lock the victim's queue
    if (pthread_mutex_trylock(&thread_data[victim_id].local_queue.mutex) == 0)
    {
      // Check if there's a task to steal
      if (thread_data[victim_id].local_queue.size > 1)
      { // Leave at least one task
        TaskNode *node = thread_data[victim_id].local_queue.head;
        thread_data[victim_id].local_queue.head = node->next;
        thread_data[victim_id].local_queue.size--;

        if (thread_data[victim_id].local_queue.size == 0)
        {
          thread_data[victim_id].local_queue.tail = NULL;
        }

        task = node->task;
        free(node);

        // 确保任务的pool指针指向正确的线程池
        // 这在窃取任务时特别重要，以确保回调能正确更新统计信息
        if (task != NULL)
        {
          task->pool = pool;
        }

        WINTERQ_LOG_INFO("Thread %d stole task from thread %d\n", thief_id, victim_id);
      }

      pthread_mutex_unlock(&thread_data[victim_id].local_queue.mutex);

      if (task != NULL)
      {
        break; // Successfully stole a task
      }
    }
  }

  return task;
}

/**
 * @brief 执行任务
 * @param runtime 执行任务的 Worker Runtime
 * @param task 要执行的任务
 */
static void execute_task(WorkerRuntime *runtime, Task *task)
{
  if (runtime == NULL || task == NULL)
    return;

  // 记录开始时间
  task->start_time = clock();

  if (task->is_script)
  {
    // 执行JavaScript脚本
    Worker_Eval_JS(runtime, task->script, task_completion_callback, task);
    if (task->script != NULL)
    {
      free((void *)task->script); // 释放脚本字符串
      task->script = NULL;
    }
  }
  else
  { // 执行JavaScript字节码
    Worker_Eval_Bytecode(runtime, task->bytecode, task->bytecode_len, task_completion_callback, task);
    if (task->bytecode != NULL)
    {
      free(task->bytecode); // 释放字节码
      task->bytecode = NULL;
    }
  }

  // 执行一次事件循环，处理可能的定时器和其他异步任务
  Worker_RunLoopOnce(runtime);

  WINTERQ_LOG_DEBUG("Task %d executed synchronous successfully.\n",
                    task->task_id);
}

// 线程工作函数
static void *worker_thread(void *arg)
{
  ThreadData *thread_data = (ThreadData *)arg;
  ThreadPool *pool = thread_data->pool;
  Task *task = NULL;
  uint64_t idle_start = 0;
  int thread_id = thread_data->thread_id;

  WorkerRuntime *wrt = Worker_NewRuntime(thread_data->max_contexts);
  if (wrt == NULL)
  {
    WINTERQ_LOG_ERROR("Failed to create worker runtime for thread %d\n", thread_id);
    return NULL;
  }

  thread_data->runtime = wrt;

  // 线程开始时为空闲状态
  atomic_store(&thread_data->idle, true);
  atomic_fetch_add(&pool->idle_thread_count, 1);
  idle_start = get_current_time_ms();

  WINTERQ_LOG_INFO("Worker thread %d started\n", thread_id);

  // 线程主循环
  while (!atomic_load(&pool->shutdown))
  {
    bool was_idle = atomic_load(&thread_data->idle);
    task = NULL;

    // 尝试获取任务，优先级：全局队列 > 本地队列 > 工作窃取
    task = dequeue_task(&pool->queue);
    if (task == NULL)
      task = dequeue_task(&thread_data->local_queue);
    if (task == NULL && pool->config.enable_work_stealing)
      task = steal_task(pool, thread_id);

    // 如果获得了任务
    if (task != NULL)
    {
      // 更新线程状态为忙碌
      if (was_idle)
      {
        atomic_store(&thread_data->idle, false);
        atomic_fetch_sub(&pool->idle_thread_count, 1);

        // 计算并累加空闲时间
        uint64_t now = get_current_time_ms();
        uint64_t idle_time = now - idle_start;
        atomic_fetch_add(&thread_data->idle_time, idle_time);

        // 记录忙碌开始时间
        idle_start = now;
      }
      // 执行任务
      execute_task(thread_data->runtime, task);

      // 更新统计信息
      atomic_fetch_add(&thread_data->tasks_processed, 1);
    }
    else
    {
      // 如果没有任务，切换到空闲状态
      if (!was_idle)
      {
        atomic_store(&thread_data->idle, true);
        atomic_fetch_add(&pool->idle_thread_count, 1);

        // 计算并累加忙碌时间
        uint64_t now = get_current_time_ms();
        uint64_t busy_time = now - idle_start;
        atomic_fetch_add(&thread_data->busy_time, busy_time);

        // 记录空闲开始时间
        idle_start = now;

        // 通知调整线程
        pthread_mutex_lock(&pool->idle_mutex);
        pthread_cond_signal(&pool->idle_cond);
        pthread_mutex_unlock(&pool->idle_mutex);
      }

      // 在空闲时处理事件循环，确保定时器能够执行
      int has_pending_events = Worker_RunLoopOnce(thread_data->runtime);

      // 只有当没有待处理事件时才休眠
      if (!has_pending_events)
      {
        // 短暂休眠以避免CPU占用过高
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 10000000; // 10ms
        nanosleep(&ts, NULL);
      }
    }
  }

  // 线程退出前清理
  WINTERQ_LOG_INFO("Worker thread %d exiting\n", thread_id);

  // 释放运行时资源
  if (thread_data->runtime != NULL)
  {
    Worker_FreeRuntime(thread_data->runtime);
    thread_data->runtime = NULL;
  }

  // 计算最终的空闲/忙碌时间
  if (atomic_load(&thread_data->idle))
  {
    uint64_t idle_time = get_current_time_ms() - idle_start;
    atomic_fetch_add(&thread_data->idle_time, idle_time);
  }
  else
  {
    uint64_t busy_time = get_current_time_ms() - idle_start;
    atomic_fetch_add(&thread_data->busy_time, busy_time);
  }

  return NULL;
}

/**
 * @brief 线程池大小调整线程, Dynamically adjusts pool size based on workload and idle thread thresholds.
 * @param arg 线程池 (ThreadPool*)
 * @return NULL
 */
static void *pool_adjuster_thread(void *arg)
{
  ThreadPool *pool = (ThreadPool *)arg;
  if (pool == NULL)
    return NULL;

  WINTERQ_LOG_INFO("Pool adjuster thread started\n");

  while (atomic_load(&pool->adjuster_running))
  {
    // 等待有空闲线程的信号
    pthread_mutex_lock(&pool->idle_mutex);
    pthread_cond_wait(&pool->idle_cond, &pool->idle_mutex);
    pthread_mutex_unlock(&pool->idle_mutex);

    // 如果线程池已关闭或不需要动态调整，则退出
    if (!atomic_load(&pool->adjuster_running) || !pool->config.dynamic_sizing)
    {
      continue;
    }

    int idle_count = atomic_load(&pool->idle_thread_count);
    int total_count = pool->thread_count;

    // 当空闲线程数超过阈值时，减少线程数
    if (idle_count > pool->config.idle_threshold && total_count > 1)
    {
      // 尝试减少一个线程
      WINTERQ_LOG_INFO("Reducing thread pool size from %d to %d\n", total_count, total_count - 1);
      resize_thread_pool(pool, total_count - 1);
    }

    // 当所有线程都忙碌且任务队列不为空时，增加线程
    if (idle_count == 0 && pool->queue.size > 0)
    {
      // 尝试增加一个线程
      WINTERQ_LOG_INFO("Increasing thread pool size from %d to %d\n", total_count, total_count + 1);
      resize_thread_pool(pool, total_count + 1);
    }

    // 防止调整过于频繁，添加延迟
    sleep(1);
  }

  WINTERQ_LOG_INFO("Pool adjuster thread exiting\n");
  return NULL;
}

/**
 * @brief 创建工作线程
 * @param pool 线程池
 * @param thread_id 线程ID
 * @return 成功返回0，失败返回-1
 */
static int create_worker_thread(ThreadPool *pool, int thread_id)
{
  if (pool == NULL || thread_id < 0 || thread_id >= pool->thread_count)
    return -1;

  ThreadData *thread_data = &pool->thread_data[thread_id];

  // 初始化线程数据
  thread_data->thread_id = thread_id;
  thread_data->pool = pool;
  thread_data->max_contexts = pool->config.max_contexts;
  thread_data->runtime = NULL;
  atomic_store(&thread_data->idle, true);
  atomic_store(&thread_data->tasks_processed, 0);
  atomic_store(&thread_data->idle_time, 0);
  atomic_store(&thread_data->busy_time, 0);

  // 初始化线程本地队列
  init_task_queue(&thread_data->local_queue, pool->config.local_queue_size);

  // 创建线程
  if (pthread_create(&pool->threads[thread_id], NULL, worker_thread, thread_data) != 0)
  {
    WINTERQ_LOG_ERROR("Failed to create worker thread %d\n", thread_id);
    return -1;
  }

  return 0;
}

/**
 * @brief 初始化线程池
 * @param config 线程池配置
 * @return 初始化成功的线程池指针，失败返回NULL
 */
ThreadPool *init_thread_pool(ThreadPoolConfig config)
{
  ThreadPool *pool = NULL;
  int i;

  // 参数校验
  if (config.thread_count <= 0)
  {
    WINTERQ_LOG_ERROR("Invalid thread count: %d\n", config.thread_count);
    return NULL;
  }

  // 分配线程池结构体
  pool = (ThreadPool *)calloc(1, sizeof(ThreadPool));
  if (pool == NULL)
  {
    WINTERQ_LOG_ERROR("Failed to allocate memory for thread pool\n");
    return NULL;
  }

  // 初始化配置
  pool->config = config;
  pool->thread_count = config.thread_count;
  pool->max_tasks = 0;
  atomic_init(&pool->shutdown, false);
  atomic_init(&pool->completed_tasks, 0);
  atomic_init(&pool->total_tasks, 0);
  atomic_init(&pool->idle_thread_count, 0);
  atomic_init(&pool->adjuster_running, false);

  // 初始化互斥锁和条件变量
  if (pthread_mutex_init(&pool->pool_mutex, NULL) != 0 ||
      pthread_mutex_init(&pool->wait_mutex, NULL) != 0 ||
      pthread_mutex_init(&pool->idle_mutex, NULL) != 0 ||
      pthread_cond_init(&pool->wait_cond, NULL) != 0 ||
      pthread_cond_init(&pool->idle_cond, NULL) != 0)
  {
    WINTERQ_LOG_ERROR("Failed to initialize mutex or condition variable\n");
    free(pool);
    return NULL;
  }

  // 初始化全局任务队列
  init_task_queue(&pool->queue, config.global_queue_size);

  // 分配线程数组
  pool->threads = (pthread_t *)calloc(config.thread_count, sizeof(pthread_t));
  if (pool->threads == NULL)
  {
    WINTERQ_LOG_ERROR("Failed to allocate memory for threads\n");
    destroy_task_queue(&pool->queue);
    free(pool);
    return NULL;
  }

  // 分配线程数据数组
  pool->thread_data = (ThreadData *)calloc(config.thread_count, sizeof(ThreadData));
  if (pool->thread_data == NULL)
  {
    WINTERQ_LOG_ERROR("Failed to allocate memory for thread data\n");
    free(pool->threads);
    destroy_task_queue(&pool->queue);
    free(pool);
    return NULL;
  }

  // 创建工作线程
  for (i = 0; i < config.thread_count; i++)
  {
    if (create_worker_thread(pool, i) != 0)
    {
      WINTERQ_LOG_ERROR("Failed to create worker thread %d\n", i);
      // 清理已创建的线程
      atomic_store(&pool->shutdown, true);
      for (int j = 0; j < i; j++)
      {
        pthread_join(pool->threads[j], NULL);
      }
      free(pool->thread_data);
      free(pool->threads);
      destroy_task_queue(&pool->queue);
      free(pool);
      return NULL;
    }
  }

  // 启动调整线程
  if (config.dynamic_sizing)
  {
    atomic_store(&pool->adjuster_running, true);
    if (pthread_create(&pool->adjuster_thread, NULL, pool_adjuster_thread, pool) != 0)
    {
      WINTERQ_LOG_ERROR("Failed to create pool adjuster thread\n");
      atomic_store(&pool->adjuster_running, false);
      // 线程池仍然可以工作，只是没有动态调整功能
    }
  }

  WINTERQ_LOG_INFO("Thread pool initialized with %d threads\n", config.thread_count);
  return pool;
}

/**
 * @brief 添加脚本任务到线程池
 * @param pool 线程池
 * @param script JavaScript脚本
 * @param callback 回调函数
 * @param callback_arg 回调函数参数
 * @return 成功返回0，失败返回-1
 */
int add_script_task_to_pool(ThreadPool *pool, const char *script,
                            void (*callback)(void *), void *callback_arg)
{
  if (pool == NULL || script == NULL)
    return -1;

  // 分配任务结构体
  Task *task = (Task *)calloc(1, sizeof(Task));
  if (task == NULL)
  {
    WINTERQ_LOG_ERROR("Failed to allocate memory for task\n");
    return -1;
  }

  // 分配并复制脚本
  task->script = strdup(script);
  if (task->script == NULL)
  {
    WINTERQ_LOG_ERROR("Failed to allocate memory for script\n");
    free(task);
    return -1;
  }

  // 初始化任务字段
  task->is_script = 1;
  task->task_id = atomic_fetch_add(&pool->total_tasks, 1);
  task->callback = callback;
  task->callback_arg = callback_arg;
  task->pool = pool; // 设置线程池指针

  // 尝试添加到全局队列
  if (enqueue_task(&pool->queue, task) != 0)
  {
    WINTERQ_LOG_ERROR("Failed to add task to pool queue\n");
    free((void *)task->script);
    free(task);
    return -1;
  }

  return 0;
}

/**
 * @brief 添加字节码任务到线程池
 * @param pool 线程池
 * @param bytecode JavaScript字节码
 * @param bytecode_len 字节码长度
 * @param callback 回调函数
 * @param callback_arg 回调函数参数
 * @return 成功返回0，失败返回-1
 */
int add_bytecode_task_to_pool(ThreadPool *pool, uint8_t *bytecode, size_t bytecode_len, void (*callback)(void *), void *callback_arg)
{
  if (pool == NULL || bytecode == NULL || bytecode_len == 0)
    return -1;

  // 分配任务结构体
  Task *task = (Task *)calloc(1, sizeof(Task));
  if (task == NULL)
  {
    WINTERQ_LOG_ERROR("Failed to allocate memory for task\n");
    return -1;
  }

  // 分配并复制字节码
  task->bytecode = (uint8_t *)malloc(bytecode_len);
  if (task->bytecode == NULL)
  {
    WINTERQ_LOG_ERROR("Failed to allocate memory for bytecode\n");
    free(task);
    return -1;
  }
  memcpy(task->bytecode, bytecode, bytecode_len);

  // 初始化任务字段
  task->is_script = 0;
  task->bytecode_len = bytecode_len;
  task->task_id = atomic_fetch_add(&pool->total_tasks, 1);
  task->callback = callback;
  task->callback_arg = callback_arg;
  task->pool = pool;

  // 尝试添加到全局队列
  if (enqueue_task(&pool->queue, task) != 0)
  {
    WINTERQ_LOG_ERROR("Failed to add task to pool queue\n");
    free(task->bytecode);
    free(task);
    return -1;
  }

  return 0;
}

// 关闭线程池
void shutdown_thread_pool(ThreadPool *pool)
{
  if (pool == NULL)
    return;

  WINTERQ_LOG_INFO("Shutting down thread pool\n");

  // 设置关闭标志
  atomic_store(&pool->shutdown, true);

  // 停止调整线程
  if (atomic_load(&pool->adjuster_running))
  {
    atomic_store(&pool->adjuster_running, false);
    pthread_cond_signal(&pool->idle_cond);
    pthread_join(pool->adjuster_thread, NULL);
  }

  // 等待所有工作线程结束
  for (int i = 0; i < pool->thread_count; i++)
  {
    pthread_join(pool->threads[i], NULL);
    destroy_task_queue(&pool->thread_data[i].local_queue);
  }

  // 输出线程池统计信息
  WINTERQ_LOG_INFO("Thread pool stats: completed tasks: %d\n",
                   atomic_load(&pool->completed_tasks));

  // 清理资源
  destroy_task_queue(&pool->queue);
  free(pool->thread_data);
  free(pool->threads);

  // 销毁同步原语
  pthread_mutex_destroy(&pool->pool_mutex);
  pthread_mutex_destroy(&pool->wait_mutex);
  pthread_mutex_destroy(&pool->idle_mutex);
  pthread_cond_destroy(&pool->wait_cond);
  pthread_cond_destroy(&pool->idle_cond);

  free(pool);

  WINTERQ_LOG_INFO("Thread pool shutdown complete\n");
}

/**
 * @brief 获取线程池统计信息
 * @param pool 线程池
 * @return 统计信息结构体
 */
ThreadPoolStats get_thread_pool_stats(ThreadPool *pool)
{
  ThreadPoolStats stats = {0};

  if (pool == NULL)
  {
    return stats;
  }

  // 锁定线程池
  pthread_mutex_lock(&pool->pool_mutex);

  // 收集基本统计信息
  stats.active_threads = pool->thread_count - atomic_load(&pool->idle_thread_count);
  stats.idle_threads = atomic_load(&pool->idle_thread_count);
  stats.queued_tasks = pool->queue.size;
  stats.completed_tasks = atomic_load(&pool->completed_tasks);

  // 计算平均执行时间和等待时间
  double total_exec_time = 0.0;
  double total_idle_time = 0.0;
  double total_busy_time = 0.0;

  for (int i = 0; i < pool->thread_count; i++)
  {
    total_idle_time += atomic_load(&pool->thread_data[i].idle_time);
    total_busy_time += atomic_load(&pool->thread_data[i].busy_time);
  }

  // 计算线程利用率
  if (total_idle_time + total_busy_time > 0)
  {
    stats.thread_utilization = (total_busy_time / (total_idle_time + total_busy_time)) * 100.0;
  }

  // 计算平均执行时间
  if (stats.completed_tasks > 0)
  {
    stats.avg_execution_time = total_exec_time / stats.completed_tasks;
  }

  pthread_mutex_unlock(&pool->pool_mutex);

  return stats;
}

/**
 * @brief 等待线程池空闲（所有任务执行完毕）
 * @param pool 线程池
 * @param timeout_ms 超时时间（毫秒），0表示无限等待
 * @return 成功返回0，超时返回1，错误返回-1
 */
int wait_for_idle(ThreadPool *pool, int timeout_ms)
{
  if (pool == NULL)
  {
    return -1;
  }

  struct timespec ts;
  int rc = 0;

  // 计算超时时间点
  if (timeout_ms > 0)
  {
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000)
    {
      ts.tv_sec += 1;
      ts.tv_nsec -= 1000000000;
    }
  }

  pthread_mutex_lock(&pool->wait_mutex);

  // 当还有未完成的任务时等待
  while (pool->queue.size > 0 ||
         atomic_load(&pool->idle_thread_count) < pool->thread_count)
  {
    if (timeout_ms > 0)
    {
      // 带超时的等待
      rc = pthread_cond_timedwait(&pool->wait_cond, &pool->wait_mutex, &ts);
      if (rc == ETIMEDOUT)
      {
        rc = 1;
        break;
      }
    }
    else
    {
      // 无限等待
      rc = pthread_cond_wait(&pool->wait_cond, &pool->wait_mutex);
    }
  }

  pthread_mutex_unlock(&pool->wait_mutex);

  return rc;
}

/**
 * @brief 调整线程池大小
 * @param pool 线程池
 * @param new_thread_count 新的线程数量
 * @return 成功返回0，失败返回-1
 */
int resize_thread_pool(ThreadPool *pool, int new_thread_count)
{
  if (pool == NULL || new_thread_count <= 0)
  {
    return -1;
  }

  pthread_mutex_lock(&pool->pool_mutex);

  int current_count = pool->thread_count;

  if (new_thread_count == current_count)
  {
    // 无需调整
    pthread_mutex_unlock(&pool->pool_mutex);
    return 0;
  }

  if (new_thread_count > current_count)
  {
    // 增加线程
    pthread_t *new_threads = (pthread_t *)realloc(pool->threads, new_thread_count * sizeof(pthread_t));
    ThreadData *new_thread_data = (ThreadData *)realloc(pool->thread_data, new_thread_count * sizeof(ThreadData));

    if (new_threads == NULL || new_thread_data == NULL)
    {
      WINTERQ_LOG_ERROR("Failed to allocate memory for additional threads\n");
      pthread_mutex_unlock(&pool->pool_mutex);
      return -1;
    }

    pool->threads = new_threads;
    pool->thread_data = new_thread_data;

    // 创建新线程
    for (int i = current_count; i < new_thread_count; i++)
    {
      if (create_worker_thread(pool, i) != 0)
      {
        WINTERQ_LOG_ERROR("Failed to create new worker thread %d\n", i);
        // 回滚到原始大小
        pool->thread_count = current_count;
        pthread_mutex_unlock(&pool->pool_mutex);
        return -1;
      }
    }

    // 更新线程计数
    pool->thread_count = new_thread_count;
  }
  else
  {
    // 减少线程（这里实现更复杂，需要安全地移除线程）

    // 1. 标记需要退出的线程
    for (int i = new_thread_count; i < current_count; i++)
    {
      // 使用一个特殊标记来通知线程退出
      pool->thread_data[i].thread_id = -1; // 负值表示线程应当退出
    }

    pthread_mutex_unlock(&pool->pool_mutex);

    // 2. 等待这些线程完成
    for (int i = new_thread_count; i < current_count; i++)
    {
      pthread_join(pool->threads[i], NULL);
      destroy_task_queue(&pool->thread_data[i].local_queue);
    }

    pthread_mutex_lock(&pool->pool_mutex);

    // 3. 更新线程计数
    pool->thread_count = new_thread_count;

    // 注意：我们不释放线程数组和线程数据数组，因为这会使内存管理复杂化
    // 在下一次增加线程池大小时，我们会重用这些空间
  }

  pthread_mutex_unlock(&pool->pool_mutex);

  WINTERQ_LOG_INFO("Thread pool resized from %d to %d threads\n", current_count, new_thread_count);
  return 0;
}

/**
 * @brief 获取指定线程的统计信息
 * @param pool 线程池
 * @param thread_id 线程ID
 * @param stats 用于存储统计信息的结构体指针
 * @return 成功返回0，失败返回-1
 */
int get_thread_stats(ThreadPool *pool, int thread_id, ThreadData *stats)
{
  if (pool == NULL || stats == NULL || thread_id < 0 || thread_id >= pool->thread_count)
  {
    return -1;
  }

  // 复制线程数据
  memcpy(stats, &pool->thread_data[thread_id], sizeof(ThreadData));

  return 0;
}
