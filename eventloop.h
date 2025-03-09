#ifndef EVENTLOOP_H
#define EVENTLOOP_H

#include "quickjs.h"

// 初始化事件循环
void init_loop();

// 执行 QuickJS 的微任务队列
void execute_microtask_timer(JSContext *ctx);

// 初始化 setTimeout 和 clearTimeout
void js_std_init_timeout(JSContext *ctx);

#endif // EVENTLOOP_H
