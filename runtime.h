#ifndef WINTERQ_RUNTIME_H
#define WINTERQ_RUNTIME_H

#include <stdint.h>

#include <quickjs.h>
#include <uv.h>

typedef struct WorkerContext WorkerContext;
typedef struct WorkerRuntime WorkerRuntime;

// Hash table for timers to allow for faster lookups when clearing
typedef struct timer_entry {
  int timer_id;
  uv_timer_t *timer;
  struct timer_entry *next;
} timer_entry;

#define TIMER_TABLE_SIZE 64
typedef struct {
  timer_entry *entries[TIMER_TABLE_SIZE];
  uv_mutex_t mutex;
} timer_table;

// Runtime statistics structure
typedef struct {
  int active_contexts;
  int max_contexts;
  int active_timers;
} WorkerRuntimeStats;

typedef struct WorkerRuntime {
  JSRuntime *js_runtime; // 每个线程一个 JSRuntime

  uv_loop_t *loop;          // uv事件循环
  uv_mutex_t context_mutex; // context 锁
  int max_contexts;         // 最多同时执行的 JSContext
  int context_count;        // 已经初始化的 context 数量

  WorkerContext *context_list; // Head of the context linked list

  int next_timer_id;

  timer_table *timer_table;
} WorkerRuntime;

typedef struct WorkerContext {
  JSContext *js_context;
  WorkerRuntime *runtime;

  void (*callback)(void *);
  void *callback_arg;

  int active_timers;
  int pending_free;

  WorkerContext *next; // Next context in the list
} WorkerContext;

/**
 * 创建一个新的运行时环境
 *
 * @param max_context 最大允许的上下文数量
 * @return 成功返回 WorkerRuntime 指针，失败返回 NULL
 */
WorkerRuntime *Worker_NewRuntime(int max_contexts);

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
 * @param callback 执行完成后的回调函数
 * @param callback_arg 回调函数的参数
 * @return 成功返回 0，失败返回非零值
 */
int Worker_Eval_JS(WorkerRuntime *wrt, const char *script,
                   void (*callback)(void *), void *callback_arg);

/**
 * 执行 JavaScript Bytecode
 *
 * @param wrt 运行时环境
 * @param bytecode JavaScript Bytecode
 * @param bytecode_len JavaScript Bytecode length
 * @param callback 执行完成后的回调函数
 * @param callback_arg 回调函数的参数
 * @return 成功返回 0，失败返回非零值
 */
int Worker_Eval_Bytecode(WorkerRuntime *wrt, uint8_t *bytecode,
                         size_t bytecode_len, void (*callback)(void *),
                         void *callback_arg);

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

void Worker_RequestContextFree(WorkerContext *wctx);
void Worker_GetRuntimeStats(WorkerRuntime *wrt, WorkerRuntimeStats *stats);
void Worker_CancelContextTimers(WorkerContext *wctx);

#endif /* WINTERQ_RUNTIME_H */
