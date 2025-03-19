#include <quickjs.h>
#include <uv.h>

#include "console.h"
#include "log.h"
#include "runtime.h"

#define MAX_MICROTASK_ITERATIONS 1000

// clang-format off
#define SAFE_FREE(ptr) do { if (ptr) { free(ptr); ptr = NULL; } } while (0)
#define SAFE_JS_FREEVALUE(ctx, val) do { if (!JS_IsUndefined(val)) { JS_FreeValue(ctx, val); } } while (0)
// clang-format on

static JSClassID js_worker_context_class_id = 0;

// 定时器结构体，用于跟踪定时器
typedef struct {
  uv_timer_t timer;
  WorkerContext *wctx;
  JSContext *ctx;
  JSValue callback;
  int timer_id;
  int is_interval;
  int delay;
} timer_data_t;

// Forward declarations
static WorkerContext *get_worker_context(JSContext *ctx);
static void execute_microtask_timer(JSContext *ctx);
static void close_timer_callback(uv_handle_t *handle);
static void init_timer_table(WorkerRuntime *wrt);
static void cleanup_timer_table(WorkerRuntime *wrt);
static void add_timer_to_table(WorkerRuntime *wrt, int timer_id,
                               uv_timer_t *timer);
static uv_timer_t *find_timer_by_id(WorkerRuntime *wrt, int timer_id);
static void remove_timer_from_table(WorkerRuntime *wrt, int timer_id);
static void close_all_handles_walk_cb(uv_handle_t *handle, void *arg);
static void count_handles_walk_cb(uv_handle_t *handle, void *arg);

WorkerRuntime *Worker_NewRuntime(int max_contexts) {
  if (max_contexts <= 0) {
    WINTERQ_LOG_ERROR("Invalid max_context value: %d", max_contexts);
    return NULL;
  }

  WorkerRuntime *wrt = calloc(1, sizeof(WorkerRuntime));
  if (!wrt) {
    WINTERQ_LOG_ERROR("Failed to allocate memory for WorkerRuntime");
    return NULL;
  }

  JSRuntime *rt = JS_NewRuntime();
  if (!rt) {
    WINTERQ_LOG_ERROR("Failed to create JS runtime");
    SAFE_FREE(wrt);
    return NULL;
  }

  uv_loop_t *loop = uv_loop_new();
  if (!loop) {
    WINTERQ_LOG_ERROR("Failed to allocate memory for event loop");
    JS_FreeRuntime(rt);
    SAFE_FREE(wrt);
    return NULL;
  }

  wrt->js_runtime = rt;
  wrt->loop = loop;
  wrt->max_contexts = max_contexts;
  wrt->context_count = 0;
  wrt->next_timer_id = 1;
  wrt->context_list = NULL;

  uv_mutex_init(&wrt->context_mutex);

  // Initialize the timer table for faster lookups
  init_timer_table(wrt);

  uv_run(wrt->loop, UV_RUN_NOWAIT);

  // Register class ID for worker context if not already done
  if (js_worker_context_class_id == 0) {
    JS_NewClassID(&js_worker_context_class_id);
  }

  return wrt;
}

void Worker_FreeRuntime(WorkerRuntime *wrt) {
  if (!wrt)
    return;

  // Close all active handles in the loop
  uv_walk(wrt->loop, close_all_handles_walk_cb, NULL);

  // 运行事件循环以处理关闭回调
  // Run the loop until all handles are closed
  // Use UV_RUN_ONCE or UV_RUN_NOWAIT to avoid blocking forever
  while (uv_run(wrt->loop, UV_RUN_ONCE) > 0) {
    // Continue processing until no more active handles
  }

  uv_mutex_lock(&wrt->context_mutex);
  WorkerContext *curr = wrt->context_list;
  uv_mutex_unlock(&wrt->context_mutex);

  // Free all contexts
  while (curr) {
    WorkerContext *next = curr->next;
    Worker_FreeContext(curr);
    curr = next;
  }

  // Clean up resources
  cleanup_timer_table(wrt);

  uv_mutex_destroy(&wrt->context_mutex);

  // 在释放 JS 运行时之前，确保所有 JS 对象都被释放
  JS_RunGC(wrt->js_runtime);

  int result = uv_loop_close(wrt->loop);
  if (result != 0) {
    WINTERQ_LOG_WARNING("Failed to close event loop: %s", uv_strerror(result));
    // 如果仍然失败，尝试获取活跃句柄数量并记录
    int handle_count = 0;
    uv_walk(wrt->loop, count_handles_walk_cb, &handle_count);
    WINTERQ_LOG_WARNING("There are still %d active handles", handle_count);

    if (handle_count > 0)
      // Last resort: force close the loop (may cause memory leaks)
      uv_loop_fork(wrt->loop); // This is a hack to force detachment of handles
  }

  if (wrt->js_runtime) {
    // JSMemoryUsage s;
    // JS_ComputeMemoryUsage(wrt->js_runtime, &s);
    // JS_DumpMemoryUsage(stdout, &s, wrt->js_runtime);
    JS_FreeRuntime(wrt->js_runtime);
    wrt->js_runtime = NULL;
  }
  SAFE_FREE(wrt->loop);
  SAFE_FREE(wrt);
}

void Worker_FreeContext(WorkerContext *wctx) {
  if (!wctx)
    return;

  // Cancel all pending timers associated with this context
  Worker_CancelContextTimers(wctx);

  // 保存回调信息，因为我们将在释放 wctx 之后调用它
  void (*callback)(void *) = wctx->callback;
  void *callback_arg = wctx->callback_arg;

  WorkerRuntime *wrt = wctx->runtime;

  // Remove from the context list
  uv_mutex_lock(&wrt->context_mutex);
  if (wrt->context_list == wctx) {
    // It's the head of the list
    wrt->context_list = wctx->next;
  } else {
    // Find it in the list
    WorkerContext *curr = wrt->context_list;
    while (curr && curr->next != wctx) {
      curr = curr->next;
    }
    if (curr) {
      curr->next = wctx->next;
    }
  }
  wrt->context_count--;
  uv_mutex_unlock(&wrt->context_mutex);
  JS_FreeContext(wctx->js_context);
  SAFE_FREE(wctx);

  // 如果有回调函数，执行回调
  if (callback)
    callback(callback_arg);
}

static WorkerContext *get_worker_context(JSContext *ctx) {
  if (!ctx) {
    WINTERQ_LOG_ERROR("NULL context passed to get_worker_context");
    return NULL;
  }
  JSValue global_obj = JS_GetGlobalObject(ctx);
  JSValue js_wctx = JS_GetPropertyStr(ctx, global_obj,
                                      "________winterq_worker_context________");
  if (JS_IsException(js_wctx) || JS_IsUndefined(js_wctx)) {
    WINTERQ_LOG_ERROR(
        "Failed to get ________winterq_worker_context________ property");
    SAFE_JS_FREEVALUE(ctx, global_obj);
    return NULL;
  }
  WorkerContext *wctx = JS_GetOpaque(js_wctx, js_worker_context_class_id);
  SAFE_JS_FREEVALUE(ctx, js_wctx);
  SAFE_JS_FREEVALUE(ctx, global_obj);
  if (!wctx) {
    WINTERQ_LOG_ERROR("Failed to get opaque worker context");
    return NULL;
  }

  return wctx;
}

// 执行 QuickJS 的微任务队列
static void execute_microtask_timer(JSContext *ctx) {
  if (!ctx) {
    WINTERQ_LOG_ERROR("NULL context passed to execute_microtask_timer");
    return;
  }
  JSContext *current_ctx = ctx; // 保存原始上下文引用
  JSRuntime *rt = JS_GetRuntime(ctx);
  if (!rt) {
    WINTERQ_LOG_ERROR("Failed to get JS runtime");
    return;
  }

  // 执行所有待处理的任务，但限制最大执行次数以避免无限循环
  int pending_jobs = 0;
  int count = 0;
  do {
    pending_jobs = JS_ExecutePendingJob(rt, &ctx);
    count++;
  } while (pending_jobs > 0 && count < MAX_MICROTASK_ITERATIONS);

  if (count >= MAX_MICROTASK_ITERATIONS && pending_jobs > 0) {
    WINTERQ_LOG_WARNING("Reached maximum microtask iterations (%d)",
                        MAX_MICROTASK_ITERATIONS);
  }

  WorkerContext *wctx = get_worker_context(current_ctx);

  // 检查是否可以释放上下文
  if (wctx && wctx->active_timers == 0 && wctx->pending_free) {
    Worker_FreeContext(wctx);
  }
}

// 释放定时器资源
static void close_timer_callback(uv_handle_t *handle) {
  if (!handle || !handle->data) {
    WINTERQ_LOG_ERROR("Invalid handle in close_timer_callback");
    return;
  }

  timer_data_t *timer_data = (timer_data_t *)handle->data;
  WorkerContext *wctx = timer_data->wctx;
  JSContext *ctx = timer_data->ctx;

  if (!wctx || !ctx) {
    WINTERQ_LOG_ERROR("Invalid worker context or JS context in timer data");
    SAFE_FREE(timer_data);
    return;
  }

  // Remove the timer from the lookup table
  remove_timer_from_table(wctx->runtime, timer_data->timer_id);

  // Clear the JS value reference first
  if (ctx && !JS_IsUndefined(timer_data->callback)) {
    // 释放JS回调函数
    SAFE_JS_FREEVALUE(ctx, timer_data->callback);
    timer_data->callback = JS_UNDEFINED;
  }

  SAFE_FREE(timer_data);

  // 减少活跃定时器计数
  wctx->active_timers--;

  // 判断是否是最后一个定时器关闭
  if (wctx->active_timers == 0) {
    // 当没有定时器时，将上下文标记为可释放
    wctx->pending_free = 1;
    execute_microtask_timer(ctx);
  }
}

// 定时器回调函数
static void timer_callback(uv_timer_t *handle) {
  if (!handle || !handle->data) {
    WINTERQ_LOG_ERROR("Invalid handle in timer_callback");
    return;
  }

  timer_data_t *timer_data = (timer_data_t *)handle->data;
  JSContext *ctx = timer_data->ctx;
  WorkerContext *wctx = timer_data->wctx;

  if (!ctx || !wctx) {
    WINTERQ_LOG_ERROR("Invalid JS context or worker context in timer data");
    uv_timer_stop(handle);
    uv_close((uv_handle_t *)handle, NULL);
    return;
  }

  // 调用JS回调函数
  JSValue ret = JS_Call(ctx, timer_data->callback, JS_UNDEFINED, 0, NULL);
  if (JS_IsException(ret)) {
    JSValue exception = JS_GetException(ctx);
    const char *str = JS_ToCString(ctx, exception);
    if (str) {
      WINTERQ_LOG_ERROR("Timer callback exception: %s", str);
      JS_FreeCString(ctx, str);
    }
    SAFE_JS_FREEVALUE(ctx, exception);
  }
  SAFE_JS_FREEVALUE(ctx, ret);

  if (timer_data->is_interval) {
    // 对于 interval 定时器，重新启动定时器
    uv_timer_start(handle, timer_callback, timer_data->delay, 0);
    return;
  }

  // 停止定时器并释放资源
  uv_timer_stop(handle);
  uv_close((uv_handle_t *)handle, close_timer_callback);

  // 当这是最后一个定时器时，考虑释放上下文
  if (wctx->active_timers == 1) // 减1后将变为0
  {
    wctx->pending_free = 1; // 标记为可释放
  }

  // 启动微任务定时器，处理可能产生的微任务
  execute_microtask_timer(ctx);
}

// setTimeout 实现
static JSValue js_set_timer(JSContext *ctx, JSValueConst this_val, int argc,
                            JSValueConst *argv, int is_interval) {
  if (argc < 2 || !JS_IsFunction(ctx, argv[0])) {
    return JS_ThrowTypeError(
        ctx, "setTimeout/setInterval requires a function and delay");
  }

  int delay = 0;
  if (JS_ToInt32(ctx, &delay, argv[1])) {
    return JS_ThrowTypeError(ctx, "Invalid delay value");
  }
  if (delay < 0) {
    delay = 0;
  }

  WorkerContext *wctx = get_worker_context(ctx);
  if (!wctx) {
    return JS_ThrowInternalError(ctx, "Worker context not found");
  }
  // 创建定时器数据
  timer_data_t *timer_data = calloc(1, sizeof(timer_data_t));
  if (!timer_data) {
    return JS_ThrowOutOfMemory(ctx);
  }

  WorkerRuntime *wrt = wctx->runtime;
  // 初始化定时器
  uv_timer_init(wrt->loop, &timer_data->timer);
  timer_data->ctx = ctx;
  timer_data->wctx = wctx;
  timer_data->callback = JS_DupValue(ctx, argv[0]);
  timer_data->is_interval = is_interval;
  timer_data->delay = delay;

  uv_mutex_lock(&wrt->context_mutex);
  // Check for timer ID overflow
  if (wrt->next_timer_id >= INT_MAX) {
    wrt->next_timer_id = 1; // Reset to 1 if we reach the maximum
  }
  timer_data->timer_id = wrt->next_timer_id++;
  uv_mutex_unlock(&wrt->context_mutex);

  timer_data->timer.data = timer_data;

  // Add to the timer lookup table
  add_timer_to_table(wrt, timer_data->timer_id, &timer_data->timer);

  // 启动定时器
  uv_timer_start(&timer_data->timer, timer_callback, delay, 0);
  wctx->active_timers++;

  return JS_NewInt32(ctx, timer_data->timer_id);
}

// setTimeout 实现
static JSValue js_setTimeout(JSContext *ctx, JSValueConst this_val, int argc,
                             JSValueConst *argv) {
  return js_set_timer(ctx, this_val, argc, argv, 0);
}

// setInterval 实现
static JSValue js_setInterval(JSContext *ctx, JSValueConst this_val, int argc,
                              JSValueConst *argv) {
  return js_set_timer(ctx, this_val, argc, argv, 1);
}

// Helper function for clearTimeout/clearInterval
static void clear_timer(WorkerRuntime *wrt, int timer_id) {
  if (!wrt)
    return;

  uv_timer_t *timer = find_timer_by_id(wrt, timer_id);
  if (timer && timer->data) {
    uv_timer_stop(timer);
    uv_close((uv_handle_t *)timer, close_timer_callback);
  }
}

// clearTimeout 实现
static JSValue js_clearTimeout(JSContext *ctx, JSValueConst this_val, int argc,
                               JSValueConst *argv) {
  if (argc < 1) {
    return JS_UNDEFINED;
  }

  WorkerContext *wctx = get_worker_context(ctx);

  if (!wctx) {
    return JS_ThrowInternalError(ctx, "Worker context not found");
  }

  int timer_id = 0;
  if (JS_ToInt32(ctx, &timer_id, argv[0])) {
    return JS_ThrowTypeError(ctx, "Invalid timer ID");
  }

  WorkerRuntime *wrt = wctx->runtime;
  clear_timer(wrt, timer_id);

  return JS_UNDEFINED;
}

// clearInterval 实现 (same as clearTimeout)
static JSValue js_clearInterval(JSContext *ctx, JSValueConst this_val, int argc,
                                JSValueConst *argv) {
  return js_clearTimeout(ctx, this_val, argc, argv);
}

static void init_timer_table(WorkerRuntime *wrt) {
  if (!wrt)
    return;

  wrt->timer_table = calloc(1, sizeof(timer_table));
  if (!wrt->timer_table) {
    WINTERQ_LOG_ERROR("Failed to allocate memory for timer table");
    return;
  }

  int result = uv_mutex_init(&wrt->timer_table->mutex);
  if (result != 0) {
    WINTERQ_LOG_ERROR("Failed to initialize timer table mutex: %s",
                      uv_strerror(result));
    SAFE_FREE(wrt->timer_table);
    return;
  }
}

static void cleanup_timer_table(WorkerRuntime *wrt) {
  if (!wrt || !wrt->timer_table)
    return;

  uv_mutex_lock(&wrt->timer_table->mutex);

  // Free all entries in the hash table
  for (int i = 0; i < TIMER_TABLE_SIZE; i++) {
    timer_entry *entry = wrt->timer_table->entries[i];
    while (entry) {
      timer_entry *next = entry->next;
      uv_timer_t *timer = entry->timer;
      if (timer && timer->data) {
        timer_data_t *timer_data = (timer_data_t *)timer->data;
        clear_timer(wrt, timer_data->timer_id);
      }

      SAFE_FREE(entry);
      entry = next;
    }
    wrt->timer_table->entries[i] = NULL;
  }

  uv_mutex_unlock(&wrt->timer_table->mutex);

  uv_mutex_destroy(&wrt->timer_table->mutex);
  SAFE_FREE(wrt->timer_table);
}

static void add_timer_to_table(WorkerRuntime *wrt, int timer_id,
                               uv_timer_t *timer) {
  if (!wrt || !wrt->timer_table || !timer)
    return;

  uv_mutex_lock(&wrt->timer_table->mutex);

  int bucket = timer_id % TIMER_TABLE_SIZE;
  timer_entry *new_entry = malloc(sizeof(timer_entry));
  if (!new_entry) {
    WINTERQ_LOG_ERROR("Failed to allocate memory for timer entry");
    uv_mutex_unlock(&wrt->timer_table->mutex);
    return;
  }

  new_entry->timer_id = timer_id;
  new_entry->timer = timer;
  new_entry->next = wrt->timer_table->entries[bucket];
  wrt->timer_table->entries[bucket] = new_entry;

  uv_mutex_unlock(&wrt->timer_table->mutex);
}

static uv_timer_t *find_timer_by_id(WorkerRuntime *wrt, int timer_id) {
  if (!wrt || !wrt->timer_table)
    return NULL;

  uv_mutex_lock(&wrt->timer_table->mutex);

  int bucket = timer_id % TIMER_TABLE_SIZE;
  timer_entry *entry = wrt->timer_table->entries[bucket];

  while (entry) {
    if (entry->timer_id == timer_id) {
      uv_timer_t *timer = entry->timer;
      uv_mutex_unlock(&wrt->timer_table->mutex);
      return timer;
    }
    entry = entry->next;
  }

  uv_mutex_unlock(&wrt->timer_table->mutex);

  return NULL;
}

static void close_all_handles_walk_cb(uv_handle_t *handle, void *arg) {
  if (!uv_is_closing(handle)) {
    // 为不同类型的句柄设置适当的关闭回调
    if (handle->type == UV_TIMER) {
      uv_timer_stop((uv_timer_t *)handle);
    }
    uv_close(handle, close_timer_callback);
  }
}

static void count_handles_walk_cb(uv_handle_t *handle, void *arg) {
  int *count = (int *)arg;
  (*count)++;

  // 输出句柄类型以便调试
  const char *type;
  switch (handle->type) {
  case UV_TIMER:
    type = "timer";
    break;
  case UV_ASYNC:
    type = "async";
    break;
  case UV_TCP:
    type = "tcp";
    break;
  // 添加其他你可能使用的句柄类型
  default:
    type = "unknown";
    break;
  }

  WINTERQ_LOG_WARNING("Active handle: %s at %p", type, (void *)handle);
}

static void remove_timer_from_table(WorkerRuntime *wrt, int timer_id) {
  if (!wrt || !wrt->timer_table)
    return;

  uv_mutex_lock(&wrt->timer_table->mutex);

  int bucket = timer_id % TIMER_TABLE_SIZE;
  timer_entry *entry = wrt->timer_table->entries[bucket];
  timer_entry *prev = NULL;

  while (entry) {
    if (entry->timer_id == timer_id) {
      if (prev) {
        prev->next = entry->next;
      } else {
        wrt->timer_table->entries[bucket] = entry->next;
      }
      SAFE_FREE(entry);
      break;
    }
    prev = entry;
    entry = entry->next;
  }

  uv_mutex_unlock(&wrt->timer_table->mutex);
}

void js_std_init_timer(JSContext *ctx) {
  if (!ctx) {
    WINTERQ_LOG_ERROR("NULL context passed to js_std_init_timeout");
    return;
  }
  JSValue global_obj = JS_GetGlobalObject(ctx);

  JS_SetPropertyStr(ctx, global_obj, "setTimeout",
                    JS_NewCFunction(ctx, js_setTimeout, "setTimeout", 2));

  JS_SetPropertyStr(ctx, global_obj, "clearTimeout",
                    JS_NewCFunction(ctx, js_clearTimeout, "clearTimeout", 1));

  JS_SetPropertyStr(ctx, global_obj, "setInterval",
                    JS_NewCFunction(ctx, js_setInterval, "setInterval", 2));

  JS_SetPropertyStr(ctx, global_obj, "clearInterval",
                    JS_NewCFunction(ctx, js_clearInterval, "clearInterval", 1));

  JS_FreeValue(ctx, global_obj);
}

WorkerContext *Worker_NewContext(WorkerRuntime *wrt) {
  if (!wrt) {
    WINTERQ_LOG_ERROR("NULL runtime passed to Worker_NewContext");
    return NULL;
  }

  uv_mutex_lock(&wrt->context_mutex);

  if (wrt->context_count >= wrt->max_contexts) {
    WINTERQ_LOG_ERROR("Maximum context count reached (%d)", wrt->max_contexts);
    uv_mutex_unlock(&wrt->context_mutex);
    return NULL;
  }

  WorkerContext *wctx = calloc(1, sizeof(WorkerContext));
  if (!wctx) {
    WINTERQ_LOG_ERROR("Failed to allocate memory for worker context");
    uv_mutex_unlock(&wrt->context_mutex);
    return NULL;
  }

  JSContext *ctx = JS_NewContext(wrt->js_runtime);
  if (!ctx) {
    WINTERQ_LOG_ERROR("Failed to create new JS context");
    SAFE_FREE(wctx);
    uv_mutex_unlock(&wrt->context_mutex);
    return NULL;
  }

  wrt->context_count++;
  wctx->js_context = ctx;
  wctx->active_timers = 0;
  wctx->runtime = wrt;
  wctx->pending_free = 0;

  wctx->next = wrt->context_list;
  wrt->context_list = wctx;

  uv_mutex_unlock(&wrt->context_mutex);

  JSValue global = JS_GetGlobalObject(ctx);

  JSValue js_wctx = JS_NewObjectClass(ctx, js_worker_context_class_id);
  JS_SetOpaque(js_wctx, wctx);
  JS_SetPropertyStr(ctx, global, "________winterq_worker_context________",
                    js_wctx);

  js_std_init_console(ctx);
  js_std_init_timer(ctx);

  SAFE_JS_FREEVALUE(ctx, global);

  return wctx;
}

int Worker_Eval_JS(WorkerRuntime *wrt, const char *script,
                   void (*callback)(void *), void *callback_arg) {
  if (!wrt) {
    WINTERQ_LOG_ERROR("NULL runtime passed to Worker_Eval_JS");
    return 1;
  }
  if (!script) {
    WINTERQ_LOG_ERROR("NULL script passed to Worker_Eval_JS");
    return 1;
  }

  WorkerContext *wctx = Worker_NewContext(wrt);
  JSContext *ctx = wctx->js_context;

  // 存储回调函数和参数
  wctx->callback = callback;
  wctx->callback_arg = callback_arg;

  JSValue result =
      JS_Eval(ctx, script, strlen(script), "<input>", JS_EVAL_TYPE_MODULE);
  if (JS_IsException(result)) {
    JSValue exc = JS_GetException(ctx);
    const char *str = JS_ToCString(ctx, exc);
    if (str) {
      WINTERQ_LOG_ERROR("JS Evaluation error: %s", str);
      JS_FreeCString(ctx, str);
    }

    SAFE_JS_FREEVALUE(ctx, exc);
    SAFE_JS_FREEVALUE(ctx, result);

    // Mark for deferred cleanup
    wctx->pending_free = 1;

    // Process any pending tasks
    execute_microtask_timer(ctx);

    return 1;
  }

  SAFE_JS_FREEVALUE(ctx, result);

  // 处理可能产生的异步任务
  execute_microtask_timer(ctx);

  // Only run GC when necessary, not on every evaluation
  if (wctx->active_timers == 0) {
    JS_RunGC(wrt->js_runtime);
    // 脚本执行完毕且没有活跃定时器，可以安全释放
    Worker_RequestContextFree(wctx);
  }

  return 0;
}

int Worker_Eval_Bytecode(WorkerRuntime *wrt, uint8_t *bytecode,
                         size_t bytecode_len, void (*callback)(void *),
                         void *callback_arg) {
  if (!wrt) {
    WINTERQ_LOG_ERROR("NULL runtime passed to Worker_Eval_Bytecode");
    return 1;
  }
  if (!bytecode || bytecode_len == 0) {
    WINTERQ_LOG_ERROR("Invalid bytecode data passed to Worker_Eval_Bytecode");
    return 1;
  }

  WorkerContext *wctx = Worker_NewContext(wrt);
  if (!wctx) {
    WINTERQ_LOG_ERROR("Failed to create new context");
    return 1;
  }

  // 存储回调函数和参数
  wctx->callback = callback;
  wctx->callback_arg = callback_arg;

  JSContext *ctx = wctx->js_context;

  // Load bytecode
  JSValue loadedVal =
      JS_ReadObject(ctx, bytecode, bytecode_len, JS_READ_OBJ_BYTECODE);
  if (JS_IsException(loadedVal)) {
    JSValue exc = JS_GetException(ctx);
    const char *str = JS_ToCString(ctx, exc);
    if (str) {
      WINTERQ_LOG_ERROR("Bytecode loading error: %s", str);
      JS_FreeCString(ctx, str);
    }
    SAFE_JS_FREEVALUE(ctx, exc);

    // Mark for deferred cleanup
    wctx->pending_free = 1;

    // Process any pending tasks
    execute_microtask_timer(ctx);

    return 1;
  }

  // Execute loaded bytecode
  JSValue result = JS_EvalFunction(ctx, loadedVal);
  if (JS_IsException(result)) {
    JSValue exc = JS_GetException(ctx);
    const char *str = JS_ToCString(ctx, exc);
    if (str) {
      WINTERQ_LOG_ERROR("Bytecode execution error: %s", str);
      JS_FreeCString(ctx, str);
    }
    SAFE_JS_FREEVALUE(ctx, exc);
    SAFE_JS_FREEVALUE(ctx, result);

    // Mark for deferred cleanup
    wctx->pending_free = 1;
    execute_microtask_timer(ctx);
    return 1;
  }

  SAFE_JS_FREEVALUE(ctx, result);

  // 处理可能产生的异步任务
  execute_microtask_timer(ctx);

  // Only run GC when necessary, not on every evaluation
  if (wctx->active_timers == 0) {
    JS_RunGC(wrt->js_runtime);
    // 脚本执行完毕且没有活跃定时器，可以安全释放
    Worker_RequestContextFree(wctx);
  }

  return 0;
}

void Worker_RunLoop(WorkerRuntime *wrt) {
  if (!wrt || !wrt->loop) {
    WINTERQ_LOG_ERROR("Invalid runtime or loop in Worker_RunLoop");
    return;
  }
  uv_run(wrt->loop, UV_RUN_DEFAULT);
}

// 允许非阻塞式运行事件循环
int Worker_RunLoopOnce(WorkerRuntime *wrt) {
  if (!wrt || !wrt->loop) {
    WINTERQ_LOG_ERROR("Invalid runtime or loop in Worker_RunLoopOnce");
    return -1;
  }
  return uv_run(wrt->loop, UV_RUN_NOWAIT);
}

// Request a context to be freed when all timers are done
void Worker_RequestContextFree(WorkerContext *wctx) {
  if (!wctx)
    return;

  wctx->pending_free = 1;

  // If there are no active timers, free immediately
  if (wctx->active_timers == 0) {
    Worker_FreeContext(wctx);
  }
}

// Get runtime statistics
void Worker_GetRuntimeStats(WorkerRuntime *wrt, WorkerRuntimeStats *stats) {
  if (!wrt || !stats)
    return;

  uv_mutex_lock(&wrt->context_mutex);

  stats->active_contexts = wrt->context_count;
  stats->max_contexts = wrt->max_contexts;

  int active_timers = 0;

  // Count active timers across all contexts
  // This would require iterating through all contexts, which we don't have
  // direct access to For now, we'll just report the timer count from the hash
  // table

  uv_mutex_unlock(&wrt->context_mutex);

  if (wrt->timer_table) {
    uv_mutex_lock(&wrt->timer_table->mutex);

    for (int i = 0; i < TIMER_TABLE_SIZE; i++) {
      timer_entry *entry = wrt->timer_table->entries[i];
      while (entry) {
        active_timers++;
        entry = entry->next;
      }
    }

    uv_mutex_unlock(&wrt->timer_table->mutex);
  }

  stats->active_timers = active_timers;
}

// Cancel all timers for a context
void Worker_CancelContextTimers(WorkerContext *wctx) {
  if (!wctx || !wctx->runtime || !wctx->runtime->timer_table)
    return;

  WorkerRuntime *wrt = wctx->runtime;

  uv_mutex_lock(&wrt->timer_table->mutex);

  // Collect all timers for this context
  for (int i = 0; i < TIMER_TABLE_SIZE; i++) {
    timer_entry **entry_ptr = &wrt->timer_table->entries[i];
    timer_entry *entry = *entry_ptr;

    while (entry) {
      timer_entry *next_entry = entry->next;
      uv_timer_t *timer = entry->timer;

      if (timer && timer->data) {
        timer_data_t *timer_data = (timer_data_t *)timer->data;
        if (timer_data->wctx == wctx) {
          clear_timer(wctx->runtime, timer_data->timer_id);

          // Remove the timer from the lookup table
          remove_timer_from_table(wctx->runtime, timer_data->timer_id);

          // For interval timers, make sure to clear the JS callback explicitly
          if (!JS_IsUndefined(timer_data->callback)) {
            SAFE_JS_FREEVALUE(timer_data->ctx, timer_data->callback);
            timer_data->callback = JS_UNDEFINED;
          }
          SAFE_FREE(timer_data);

          // 从链表中移除此项
          *entry_ptr = next_entry;
          SAFE_FREE(entry);
        } else {
          // 只有当当前项保留在链表中时才更新entry_ptr
          entry_ptr = &entry->next;
        }
      } else {
        // 处理无效定时器数据的情况
        *entry_ptr = next_entry;
        SAFE_FREE(entry);
      }

      entry = next_entry;
    }
  }

  uv_mutex_unlock(&wrt->timer_table->mutex);

  // 确保上下文知道它没有活跃定时器了
  wctx->active_timers = 0;
}
