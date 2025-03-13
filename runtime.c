#include <quickjs.h>
#include <uv.h>

#include "runtime.h"
#include "console.h"

static JSClassID js_worker_context_class_id;

// 定时器结构体，用于跟踪定时器
typedef struct
{
  uv_timer_t timer;
  WorkerContext *wctx;
  JSContext *ctx;
  JSValue callback;
  int timer_id;
} timer_data_t;

WorkerRuntime *Worker_NewRuntime(int max_context)
{
  WorkerRuntime *wrt = malloc(sizeof(WorkerRuntime));
  if (!wrt)
  {
    return NULL;
  }
  JSRuntime *rt = JS_NewRuntime();
  if (!rt)
  {
    free(wrt);
    return NULL;
  }

  uv_loop_t *loop = uv_loop_new();
  if (!loop)
  {
    JS_FreeRuntime(rt);
    free(wrt);
    return NULL;
  }

  wrt->js_runtime = rt;
  wrt->loop = loop;
  wrt->max_context = max_context;
  wrt->context_count = 0;
  wrt->next_timer_id = 1;

  uv_mutex_init(&wrt->context_mutex);
  // 初始化微任务定时器
  uv_timer_init(loop, &wrt->microtask_timer);

  uv_run(wrt->loop, UV_RUN_NOWAIT);

  return wrt;
}

void Worker_FreeRuntime(WorkerRuntime *wrt)
{
  uv_timer_stop(&wrt->microtask_timer);
  uv_mutex_destroy(&wrt->context_mutex);
  uv_loop_close(wrt->loop);
  JS_FreeRuntime(wrt->js_runtime);
  free(wrt);
}

void Worker_FreeContext(WorkerContext *wctx)
{
  JS_FreeContext(wctx->js_context);
  uv_mutex_lock(&wctx->runtime->context_mutex);
  wctx->runtime->context_count--;
  uv_mutex_unlock(&wctx->runtime->context_mutex);
  free(wctx);
}

static WorkerContext *get_worker_context(JSContext *ctx)
{
  JSValue global_obj = JS_GetGlobalObject(ctx);
  JSValue js_wctx = JS_GetPropertyStr(ctx, global_obj, "__worker_context__");
  if (JS_IsException(js_wctx) || JS_IsUndefined(js_wctx))
  {
    fprintf(stderr, "Failed to get __worker_context__ property\n");
    JS_FreeValue(ctx, global_obj);
    return NULL;
  }
  WorkerContext *wctx = JS_GetOpaque(js_wctx, js_worker_context_class_id);
  JS_FreeValue(ctx, js_wctx);
  JS_FreeValue(ctx, global_obj);
  if (!wctx)
  {
    fprintf(stderr, "Failed to get opaque wrt\n");
    return NULL;
  }

  return wctx;
}

// 执行 QuickJS 的微任务队列
void execute_microtask_timer(JSContext *ctx)
{
  JSContext *current_ctx = ctx; // 保存原始上下文引用
  JSRuntime *rt = JS_GetRuntime(ctx);

  // 执行所有待处理的任务，但限制最大执行次数以避免无限循环
  int hasPending;
  int max_iterations = 1000; // 设置一个合理的上限
  int count = 0;
  do
  {
    hasPending = JS_ExecutePendingJob(rt, &ctx);
    count++;
  } while (hasPending > 0 && count < max_iterations);

  if (count >= max_iterations && hasPending > 0)
  {
    fprintf(stderr, "Warning: Reached maximum microtask iterations\n");
  }

  WorkerContext *wctx = get_worker_context(current_ctx);
  // 检查是否可以释放上下文
  if (wctx && wctx->active_timers == 0)
  {
    Worker_FreeContext(wctx);
  }
}

// 释放定时器资源
void close_timer_callback(uv_handle_t *handle)
{
  timer_data_t *timer_data = (timer_data_t *)handle->data;
  WorkerContext *wctx = timer_data->wctx;
  JSContext *ctx = timer_data->ctx;

  // 释放JS回调函数
  JS_FreeValue(ctx, timer_data->callback);
  free(timer_data);
  // 减少活跃定时器计数
  wctx->active_timers--;

  // 如果这是最后一个定时器，执行微任务并可能释放上下文
  if (wctx->active_timers == 0)
  {
    execute_microtask_timer(ctx);
  }
}

// 定时器回调函数
void timer_callback(uv_timer_t *handle)
{
  timer_data_t *timer_data = (timer_data_t *)handle->data;
  JSContext *ctx = timer_data->ctx;
  JSValue ret;

  // 调用JS回调函数
  ret = JS_Call(ctx, timer_data->callback, JS_UNDEFINED, 0, NULL);
  if (JS_IsException(ret))
  {
    JSValue exception = JS_GetException(ctx);
    const char *str = JS_ToCString(ctx, exception);
    printf("Timer callback exception: %s\n", str);
    JS_FreeCString(ctx, str);
    JS_FreeValue(ctx, exception);
  }
  JS_FreeValue(ctx, ret);

  // 停止定时器并释放资源
  uv_timer_stop(handle);
  uv_close((uv_handle_t *)handle, close_timer_callback);

  // 启动微任务定时器，处理可能产生的微任务
  execute_microtask_timer(ctx);
}

// setTimeout 实现
static JSValue js_setTimeout(JSContext *ctx, JSValueConst this_val, int argc,
                             JSValueConst *argv)
{
  if (argc < 2 || !JS_IsFunction(ctx, argv[0]))
  {
    return JS_ThrowTypeError(ctx, "setTimeout requires a function and delay");
  }

  int delay = 0;
  if (JS_ToInt32(ctx, &delay, argv[1]))
  {
    return JS_ThrowTypeError(ctx, "Invalid delay value");
  }

  WorkerContext *wctx = get_worker_context(ctx);

  if (!wctx)
  {
    return JS_ThrowInternalError(ctx, "Worker context not found");
  }

  // 创建定时器数据
  timer_data_t *timer_data = malloc(sizeof(timer_data_t));
  if (!timer_data)
  {
    return JS_ThrowOutOfMemory(ctx);
  }

  WorkerRuntime *wrt = wctx->runtime;
  // 初始化定时器
  uv_timer_init(wrt->loop, &timer_data->timer);
  timer_data->ctx = ctx;
  timer_data->wctx = wctx;
  timer_data->callback = JS_DupValue(ctx, argv[0]);
  uv_mutex_lock(&wrt->context_mutex);
  timer_data->timer_id = wrt->next_timer_id++;
  uv_mutex_unlock(&wrt->context_mutex);
  timer_data->timer.data = timer_data;

  // 启动定时器
  uv_timer_start(&timer_data->timer, timer_callback, delay, 0);
  wctx->active_timers++;

  return JS_NewInt32(ctx, timer_data->timer_id);
}

// Helper function for clearTimeout
void clear_timeout_walk(uv_handle_t *handle, void *arg)
{
  int *id_ptr = (int *)arg;
  if (handle->type == UV_TIMER && handle->data)
  {
    timer_data_t *timer_data = (timer_data_t *)handle->data;
    if (timer_data->timer_id == *id_ptr)
    {
      uv_timer_stop(&timer_data->timer);
      uv_close((uv_handle_t *)&timer_data->timer, close_timer_callback);
    }
  }
}

// clearTimeout 实现
static JSValue js_clearTimeout(JSContext *ctx, JSValueConst this_val, int argc,
                               JSValueConst *argv)
{

  if (argc < 1)
  {
    return JS_UNDEFINED;
  }

  WorkerContext *wctx = get_worker_context(ctx);

  if (!wctx)
  {
    return JS_ThrowInternalError(ctx, "Worker context not found");
  }

  int timer_id = 0;
  if (JS_ToInt32(ctx, &timer_id, argv[0]))
  {
    return JS_ThrowTypeError(ctx, "Invalid timer ID");
  }

  WorkerRuntime *wrt = wctx->runtime;

  // 遍历所有活跃的定时器，查找匹配的ID
  uv_walk(wrt->loop, clear_timeout_walk, &timer_id);

  return JS_UNDEFINED;
}

void js_std_init_timeout(JSContext *ctx)
{
  JSValue global_obj = JS_GetGlobalObject(ctx);

  JS_SetPropertyStr(ctx, global_obj, "setTimeout",
                    JS_NewCFunction(ctx, js_setTimeout, "setTimeout", 2));

  JS_SetPropertyStr(ctx, global_obj, "clearTimeout",
                    JS_NewCFunction(ctx, js_clearTimeout, "clearTimeout", 1));

  JS_FreeValue(ctx, global_obj);
}

WorkerContext *Worker_NewContext(WorkerRuntime *wrt)
{
  uv_mutex_lock(&wrt->context_mutex);
  if (wrt->context_count >= wrt->max_context)
  {
    uv_mutex_unlock(&wrt->context_mutex);
    return NULL;
  }

  WorkerContext *wctx = malloc(sizeof(WorkerContext));
  if (!wctx)
  {
    return NULL;
  }
  JSContext *ctx = JS_NewContext(wrt->js_runtime);
  wrt->context_count++;
  wctx->js_context = ctx;
  wctx->active_timers = 0;
  wctx->runtime = wrt;
  uv_mutex_unlock(&wrt->context_mutex);

  JSValue global = JS_GetGlobalObject(ctx);

  JSValue js_wctx = JS_NewObjectClass(ctx, js_worker_context_class_id);
  JS_SetOpaque(js_wctx, wctx);
  JS_SetPropertyStr(ctx, global, "__worker_context__", js_wctx);

  js_std_init_console(ctx);
  js_std_init_timeout(ctx);

  JS_FreeValue(ctx, global);

  return wctx;
}

int Worker_Eval_JS(WorkerRuntime *wrt, char *script)
{
  WorkerContext *wctx = Worker_NewContext(wrt);
  if (!wctx)
  {
    fprintf(stderr, "Failed to create new context\n");
    return 1;
  }

  JSContext *ctx = wctx->js_context;
  JSValue result = JS_Eval(ctx, script, strlen(script), "<input>", JS_EVAL_TYPE_MODULE);
  if (JS_IsException(result))
  {
    JSValue exc = JS_GetException(ctx);
    const char *str = JS_ToCString(ctx, exc);
    if (str)
    {
      fprintf(stderr, "Error: %s\n", str);
      JS_FreeCString(ctx, str);
    }
    JS_FreeValue(ctx, exc);
    JS_FreeValue(ctx, result);
    return 1;
  }
  JS_FreeValue(ctx, result);

  // 处理可能产生的异步任务
  execute_microtask_timer(ctx);

  return 0;
}

int Worker_Eval_Bytecode(WorkerRuntime *wrt, uint8_t *bytecode, size_t bytecode_len)
{
  WorkerContext *wctx = Worker_NewContext(wrt);
  if (!wctx)
  {
    fprintf(stderr, "Failed to create new context\n");
    return 1;
  }

  JSContext *ctx = wctx->js_context;

  // Load bytecode
  JSValue loadedVal = JS_ReadObject(ctx, bytecode, bytecode_len, JS_READ_OBJ_BYTECODE);
  if (JS_IsException(loadedVal))
  {
    JSValue exc = JS_GetException(ctx);
    const char *str = JS_ToCString(ctx, exc);
    if (str)
    {
      fprintf(stderr, "Error: %s\n", str);
      JS_FreeCString(ctx, str);
    }
    JS_FreeValue(ctx, exc);
    return 1;
  }

  // Execute loaded bytecode
  JSValue result = JS_EvalFunction(ctx, loadedVal);
  if (JS_IsException(result))
  {
    JSValue exc = JS_GetException(ctx);
    const char *str = JS_ToCString(ctx, exc);
    if (str)
    {
      fprintf(stderr, "Error: %s\n", str);
      JS_FreeCString(ctx, str);
    }
    JS_FreeValue(ctx, exc);
    JS_FreeValue(ctx, result);
    return 1;
  }
  JS_FreeValue(ctx, result);

  // 处理可能产生的异步任务
  execute_microtask_timer(ctx);

  return 0;
}

void Worker_RunLoop(WorkerRuntime *wrt)
{
  uv_run(wrt->loop, UV_RUN_DEFAULT);
}

// 允许非阻塞式运行事件循环
int Worker_RunLoopOnce(WorkerRuntime *wrt)
{
  return uv_run(wrt->loop, UV_RUN_NOWAIT);
}
