#include <stdio.h>
#include <stdlib.h>

#include "eventloop.h"

// 定时器结构体，用于跟踪定时器
typedef struct
{
  uv_timer_t timer;
  JSContext *ctx;
  JSValue callback;
  int timer_id;
} timer_data_t;

// 全局变量
static uv_loop_t *loop;
static int next_timer_id = 1;
static int active_timers = 0;
static uv_timer_t microtask_timer; // 用于执行微任务的定时器

void init_loop()
{
  loop = uv_default_loop();
  // 初始化微任务定时器
  uv_timer_init(loop, &microtask_timer);
}

// 执行 QuickJS 的微任务队列
void execute_microtask_timer(JSContext *ctx)
{
  JSRuntime *rt = JS_GetRuntime(ctx);

  // 执行所有待处理的任务
  int hasPending;
  do
  {
    hasPending = JS_ExecutePendingJob(rt, &ctx);
  } while (hasPending > 0);

  // 如果没有更多待处理任务，停止定时器
  // uv_timer_stop(&microtask_timer);
}

// 释放定时器资源
void close_timer_callback(uv_handle_t *handle)
{
  timer_data_t *timer_data = (timer_data_t *)handle->data;
  JSContext *ctx = timer_data->ctx;

  // 释放JS回调函数
  JS_FreeValue(ctx, timer_data->callback);
  free(timer_data);

  // 减少活跃定时器计数
  active_timers--;
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

  // 创建定时器数据
  timer_data_t *timer_data = malloc(sizeof(timer_data_t));
  if (!timer_data)
  {
    return JS_ThrowOutOfMemory(ctx);
  }

  // 初始化定时器
  uv_timer_init(loop, &timer_data->timer);
  timer_data->ctx = ctx;
  timer_data->callback = JS_DupValue(ctx, argv[0]);
  timer_data->timer_id = next_timer_id++;
  timer_data->timer.data = timer_data;

  // 启动定时器
  uv_timer_start(&timer_data->timer, timer_callback, delay, 0);
  active_timers++;

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

  int timer_id = 0;
  if (JS_ToInt32(ctx, &timer_id, argv[0]))
  {
    return JS_ThrowTypeError(ctx, "Invalid timer ID");
  }

  // 遍历所有活跃的定时器，查找匹配的ID
  uv_walk(loop, clear_timeout_walk, &timer_id);

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
