#ifndef WINTERQ_EVENT_H
#define WINTERQ_EVENT_H

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "quickjs.h"

// 定义事件常量
typedef enum EventPhaseEnum {
  EVENT_NONE,
  EVENT_CAPTURING_PHASE,
  EVENT_AT_TARGET,
  EVENT_BUBBLING_PHASE,
} EventPhaseEnum;

// 事件监听器结构
typedef struct EventListener {
  JSValue callback;           // 回调函数
  char *type;                 // 事件类型名称
  bool capture;               // 是否捕获阶段
  bool passive;               // 是否被动监听
  bool once;                  // 是否仅触发一次
  bool removed;               // 是否已移除
  struct EventListener *next; // 链表下一个元素
} EventListener;

// 事件结构
typedef struct {
  JSValue target;                // 目标对象
  JSValue currentTarget;         // 当前目标
  JSValue relatedTarget;         // 相关目标
  char *type;                    // 事件类型
  bool isCustom;                 // 是否自定义事件
  bool bubbles;                  // 是否冒泡
  bool cancelable;               // 是否可取消
  bool composed;                 // 是否可穿透Shadow DOM
  bool defaultPrevented;         // 是否已阻止默认行为
  bool stopPropagation;          // 是否停止传播
  bool stopImmediatePropagation; // 是否立即停止传播
  bool isTrusted;                // 是否可信(由UA触发)
  double timeStamp;              // 事件创建时间
  EventPhaseEnum eventPhase;     // 事件传播阶段（捕获、目标、冒泡）
  JSValue detail;                // 自定义数据(用于CustomEvent)
} Event;

// EventTarget结构
typedef struct {
  EventListener *listeners; // 监听器链表
} EventTarget;

void js_init_event(JSContext *ctx);

#endif // WINTERQ_EVENT_H
