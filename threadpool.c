#include <uv.h>

#include "threadpool.h"
#include "eventloop.h"
#include "console.h"

// 初始化任务队列
static void init_task_queue(TaskQueue *queue)
{
  queue->head = NULL;
  queue->tail = NULL;
  queue->size = 0;
  uv_mutex_init(&queue->mutex);
  uv_cond_init(&queue->not_empty);
}

// 添加任务到队列
static void enqueue_task(TaskQueue *queue, Task task)
{
  TaskNode *node = (TaskNode *)malloc(sizeof(TaskNode));
  node->task = task;
  node->next = NULL;

  uv_mutex_lock(&queue->mutex);

  if (queue->tail == NULL)
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
  uv_cond_signal(&queue->not_empty);
  uv_mutex_unlock(&queue->mutex);
}

// 销毁任务队列
static void destroy_task_queue(TaskQueue *queue)
{
  uv_mutex_lock(&queue->mutex);

  TaskNode *current = queue->head;
  while (current != NULL)
  {
    TaskNode *next = current->next;
    free(current);
    current = next;
  }

  queue->head = NULL;
  queue->tail = NULL;
  queue->size = 0;

  uv_mutex_unlock(&queue->mutex);
  uv_mutex_destroy(&queue->mutex);
  uv_cond_destroy(&queue->not_empty);
}

// Function to evaluate a JS Code with QuickJS
static int eval_js(JSContext *ctx, const char *js_code)
{
  // Evaluate JS code
  JSValue val =
      JS_Eval(ctx, js_code, strlen(js_code), js_code, JS_EVAL_TYPE_GLOBAL);

  if (JS_IsException(val))
  {
    if (JS_IsException(JS_GetException(ctx)))
    {
      JSValue exc = JS_GetException(ctx);
      const char *str = JS_ToCString(ctx, exc);
      if (str)
      {
        fprintf(stderr, "Error: %s\n", str);
        JS_FreeCString(ctx, str);
      }
      JS_FreeValue(ctx, exc);
    }
    return 1;
  }
  JS_FreeValue(ctx, val);
  return 0;
}

// 执行任务
static void execute_task(JSRuntime *runtime, Task *task)
{
  clock_t start, end;
  start = clock();

  // 为任务创建新的 JSContext
  JSContext *ctx = JS_NewContext(runtime);
  if (!ctx)
  {
    fprintf(stderr, "Failed to create JS context for task %d\n", task->task_id);
    return;
  }

  js_std_init_console(ctx);
  js_std_init_timeout(ctx);

  if (eval_js(ctx, task->js_code) < 0)
  {
    fprintf(stderr, "Error executing js in task %d\n",
            task->task_id);
  }

  // 清理 JSContext
  JS_FreeContext(ctx);
  JS_RunGC(JS_GetRuntime(ctx));

  end = clock();
  task->execution_time = ((double)(end - start)) / CLOCKS_PER_SEC;
}

// 线程工作函数
static void worker_thread(void *arg)
{
  ThreadData *thread_data = (ThreadData *)arg;
  ThreadPool *pool = thread_data->pool;
  int thread_id = thread_data->thread_id;

  // 每个线程创建自己的 JSRuntime
  JSRuntime *runtime = JS_NewRuntime();
  if (!runtime)
  {
    fprintf(stderr, "Failed to create JS runtime for thread %d\n", thread_id);
    return;
  }

  thread_data->runtime = runtime;

  printf("Thread %d started with its own JSRuntime\n", thread_id);

  // 循环处理任务
  while (1)
  {
    Task task;
    int got_task = 0;

    // 获取任务，使用条件变量等待
    uv_mutex_lock(&pool->queue.mutex);

    // 检查是否需要关闭线程
    uv_mutex_lock(&pool->shutdown_mutex);
    int shutdown = pool->shutdown;
    uv_mutex_unlock(&pool->shutdown_mutex);

    // 如果没有任务且收到关闭信号，退出循环
    if (pool->queue.size == 0 && shutdown)
    {
      uv_mutex_unlock(&pool->queue.mutex);
      break;
    }

    // 如果没有任务，等待任务或关闭信号
    while (pool->queue.size == 0 && !shutdown)
    {
      uv_cond_wait(&pool->queue.not_empty, &pool->queue.mutex);

      // 再次检查关闭信号
      uv_mutex_lock(&pool->shutdown_mutex);
      shutdown = pool->shutdown;
      uv_mutex_unlock(&pool->shutdown_mutex);
    }

    // 如果收到关闭信号且没有任务，退出循环
    if (pool->queue.size == 0 && shutdown)
    {
      uv_mutex_unlock(&pool->queue.mutex);
      break;
    }

    // 获取任务
    if (pool->queue.head != NULL)
    {
      TaskNode *node = pool->queue.head;
      task = node->task;

      pool->queue.head = node->next;
      if (pool->queue.head == NULL)
      {
        pool->queue.tail = NULL;
      }

      pool->queue.size--;
      free(node);
      got_task = 1;
    }

    uv_mutex_unlock(&pool->queue.mutex);

    if (got_task)
    {
      // 执行任务
      execute_task(runtime, &task);

      // 将任务结果存储到主线程的任务数组中
      uv_mutex_lock(&pool->completed_mutex);

      // 动态扩展任务结果数组
      if (task.task_id > pool->max_tasks)
      {
        int old_max = pool->max_tasks;
        pool->max_tasks = task.task_id;
        pool->task_execution_times = realloc(pool->task_execution_times,
                                             pool->max_tasks * sizeof(TaskExecutionTime));
        // 初始化新分配的内存
        for (int i = old_max; i < pool->max_tasks; i++)
        {
          pool->task_execution_times[i].task_id = 0;
          pool->task_execution_times[i].execution_time = 0.0;
        }
      }

      pool->task_execution_times[task.task_id - 1].task_id = task.task_id;
      pool->task_execution_times[task.task_id - 1].execution_time = task.execution_time;
      pool->completed_tasks++;

      uv_cond_signal(&pool->all_completed);
      uv_mutex_unlock(&pool->completed_mutex);

      // 执行回调
      if (task.callback)
      {
        task.callback(&task);
      }
    }
  }

  // 清理 JSRuntime
  JS_FreeRuntime(runtime);
  printf("Thread %d shutting down\n", thread_id);
}

// 初始化线程池
ThreadPool *init_thread_pool(int thread_count)
{
  ThreadPool *pool = (ThreadPool *)malloc(sizeof(ThreadPool));
  if (!pool)
  {
    fprintf(stderr, "Failed to allocate memory for thread pool\n");
    return NULL;
  }

  pool->thread_count = thread_count;
  pool->threads = (uv_thread_t *)malloc(thread_count * sizeof(uv_thread_t));
  pool->shutdown = 0;
  pool->completed_tasks = 0;
  pool->max_tasks = 0;
  pool->task_execution_times = NULL;

  uv_mutex_init(&pool->shutdown_mutex);
  uv_mutex_init(&pool->completed_mutex);
  uv_cond_init(&pool->all_completed);

  init_task_queue(&pool->queue);

  // 创建线程数据
  ThreadData *thread_data =
      (ThreadData *)malloc(thread_count * sizeof(ThreadData));

  // 创建工作线程
  for (int i = 0; i < thread_count; i++)
  {
    thread_data[i].pool = pool;
    thread_data[i].thread_id = i;
    thread_data[i].runtime = NULL;

    if (uv_thread_create(&pool->threads[i], worker_thread,
                         &thread_data[i]) != 0)
    {
      fprintf(stderr, "Failed to create thread %d\n", i);
      // 清理已创建的线程
      for (int j = 0; j < i; j++)
      {
        uv_thread_join(&pool->threads[j]);
      }

      free(pool->threads);
      free(thread_data);
      free(pool);
      return NULL;
    }
  }

  return pool;
}

// 添加任务到线程池
void add_task_to_pool(ThreadPool *pool, const char *js_code, void (*callback)(void *), void *callback_arg)
{
  static int task_id = 1;
  Task task;
  task.js_code = js_code;
  task.execution_time = 0.0;
  task.task_id = task_id++;
  task.callback = callback;
  task.callback_arg = callback_arg;
  enqueue_task(&pool->queue, task);
}

// 关闭线程池
void shutdown_thread_pool(ThreadPool *pool)
{
  if (!pool)
    return;

  // 设置关闭标志
  uv_mutex_lock(&pool->shutdown_mutex);
  pool->shutdown = 1;
  uv_mutex_unlock(&pool->shutdown_mutex);

  // 唤醒所有等待任务的线程
  uv_mutex_lock(&pool->queue.mutex);
  uv_cond_broadcast(&pool->queue.not_empty);
  uv_mutex_unlock(&pool->queue.mutex);

  // 等待所有线程结束
  for (int i = 0; i < pool->thread_count; i++)
  {
    uv_thread_join(&pool->threads[i]);
  }

  // 清理资源
  destroy_task_queue(&pool->queue);
  uv_mutex_destroy(&pool->shutdown_mutex);
  uv_mutex_destroy(&pool->completed_mutex);
  uv_cond_destroy(&pool->all_completed);

  free(pool->task_execution_times);
  free(pool->threads);
  free(pool);
}
