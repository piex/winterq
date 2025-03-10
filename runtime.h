#ifndef WINTERQ_RUNTIME_H
#define WINTERQ_RUNTIME_H

#include <uv.h>
#include <quickjs.h>

typedef struct
{
  JSRuntime *js_runtime; // 每个线程一个 JSRuntime

  int max_context;          // 最多同时执行的 JSContext
  int context_count;        // 已经初始化的 context 数量
  uv_mutex_t context_mutex; // context 锁

  uv_loop_t *loop; // uv事件循环
  int next_timer_id;
  uv_timer_t microtask_timer; // 用于执行微任务的定时器
} WorkerRuntime;

typedef struct
{
  JSContext *js_context;
  int active_timers;

  WorkerRuntime *runtime;
} WorkerContext;

/**
 * 创建一个新的运行时环境
 *
 * @param max_context 最大允许的上下文数量
 * @return 成功返回 WorkerRuntime 指针，失败返回 NULL
 */
WorkerRuntime *Worker_NewRuntime(int max_context);

/**
 * 释放运行时环境资源
 *
 * @param wrt 要释放的运行时环境
 */
void Worker_FreeRuntime(WorkerRuntime *wrt);

/**
 * 创建一个新的 JavaScript 执行上下文
 *
 * @param wrt 运行时环境
 * @return 成功返回 WorkerContext 指针，失败返回 NULL
 */
WorkerContext *Worker_NewContext(WorkerRuntime *wrt);

/**
 * 释放上下文资源
 *
 * @param wctx 要释放的上下文
 */
void Worker_FreeContext(WorkerContext *wctx);

/**
 * 执行 JavaScript 代码
 *
 * @param wrt 运行时环境
 * @param script JavaScript 代码字符串
 * @return 成功返回 0，失败返回非零值
 */
int Worker_Eval_JS(WorkerRuntime *wrt, char *script);

/**
 * 运行事件循环，阻塞直到所有事件处理完毕
 *
 * @param wrt 运行时环境
 */
void Worker_RunLoop(WorkerRuntime *wrt);

/**
 * 运行事件循环一次，非阻塞
 *
 * @param wrt 运行时环境
 * @return 返回是否还有待处理的事件
 */
int Worker_RunLoopOnce(WorkerRuntime *wrt);

#endif /* WINTERQ_RUNTIME_H */
