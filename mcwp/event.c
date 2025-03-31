#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "quickjs.h"

#include "event.h"

JSClassID js_event_class_id = 0;
JSClassID js_custom_event_class_id = 0;
JSClassID js_event_target_class_id = 0;

static Event *get_event(JSValueConst this_val) {
  Event *event = JS_GetOpaque(this_val, js_event_class_id);
  if (!event)
    event = JS_GetOpaque(this_val, js_custom_event_class_id);
  return event;
}

// 查找事件监听器
static EventListener *find_event_listener(EventTarget *target, const char *type, JSValue callback, bool capture) {
  EventListener *listener = target->listeners;
  while (listener) {
    if (listener->capture == capture && !strcmp(listener->type, type) && JS_StrictEq(NULL, listener->callback, callback)) {
      return listener;
    }
    listener = listener->next;
  }
  return NULL;
}

// Event构造函数
static JSValue js_event_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv, int magic) {
  JSValue obj = JS_UNDEFINED;
  Event *event = NULL;
  const char *type = "";
  bool bubbles = false;
  bool cancelable = false;
  bool composed = false;
  bool isCustom = magic == 1;
  JSValue detail = JS_NULL;

  if (JS_IsUndefined(new_target))
    return JS_ThrowTypeError(ctx, "Constructor Event requires 'new'");

  if (argc < 1)
    return JS_ThrowTypeError(ctx, "1 argument required, but only 0 present");

  // 创建JS对象
  obj = JS_NewObjectClass(ctx, !isCustom ? js_event_class_id : js_custom_event_class_id);
  if (JS_IsException(obj))
    goto fail;

  // 解析参数
  type = JS_ToCString(ctx, argv[0]);
  if (!type)
    goto fail;

  if (argc > 1 && JS_IsObject(argv[1])) {
    JSValue bubbles_val = JS_GetPropertyStr(ctx, argv[1], "bubbles");
    if (!JS_IsException(bubbles_val))
      bubbles = JS_ToBool(ctx, bubbles_val);
    JS_FreeValue(ctx, bubbles_val);

    JSValue cancelable_val = JS_GetPropertyStr(ctx, argv[1], "cancelable");
    if (!JS_IsException(cancelable_val))
      cancelable = JS_ToBool(ctx, cancelable_val);
    JS_FreeValue(ctx, cancelable_val);

    JSValue composed_val = JS_GetPropertyStr(ctx, argv[1], "composed");
    if (!JS_IsException(composed_val))
      composed = JS_ToBool(ctx, composed_val);
    JS_FreeValue(ctx, composed_val);

    JSValue detail_val = JS_GetPropertyStr(ctx, argv[1], "detail");
    if (!JS_IsException(detail_val)) {
      detail = JS_DupValue(ctx, detail_val);
    }
    JS_FreeValue(ctx, detail_val);
  }

  // 创建Event对象
  event = calloc(1, sizeof(Event));
  if (!event)
    goto fail;

  event->isCustom = isCustom;
  event->type = strdup(type);
  event->bubbles = bubbles;
  event->cancelable = cancelable;
  event->composed = composed;
  event->timeStamp = clock();
  event->eventPhase = EVENT_NONE;
  event->target = JS_NULL;
  event->currentTarget = JS_NULL;
  event->relatedTarget = JS_UNDEFINED;
  event->detail = detail;
  event->isTrusted = false;

  JS_SetOpaque(obj, event);

  if (type)
    JS_FreeCString(ctx, type);

  return obj;

fail:
  if (type)
    JS_FreeCString(ctx, type);
  if (event) {
    if (event->type)
      free(event->type);
    free(event);
  }
  if (!JS_IsNull(detail) || !JS_IsUndefined(detail)) {
    JS_FreeValue(ctx, detail);
  }
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

// 清理函数
static void js_event_finalizer(JSRuntime *rt, JSValue val) {
  Event *event = get_event(val);

  if (event) {
    if (event->type)
      free(event->type);
    // 释放其他JS值
    JS_FreeValueRT(rt, event->target);
    JS_FreeValueRT(rt, event->currentTarget);
    JS_FreeValueRT(rt, event->relatedTarget);
    JS_FreeValueRT(rt, event->detail);
    free(event);
  }
}

// GC 标记函数 - 标记我们对象中引用的 JavaScript 值
static void js_event_gc_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark_func) {
  Event *event = get_event(val);
  if (event) {
    if (!JS_IsNull(event->target)) {
      JS_MarkValue(rt, event->target, mark_func);
    }
  }
}

// Event getter方法
static JSValue js_event_get_property(JSContext *ctx, JSValueConst this_val, int magic) {
  Event *event = get_event(this_val);
  if (!event)
    return JS_EXCEPTION;

  switch (magic) {
  case 0:
    return JS_NewString(ctx, event->type ? event->type : "");
  case 1:
    return JS_DupValue(ctx, event->target);
  case 2:
    return JS_DupValue(ctx, event->currentTarget);
  case 3:
    return JS_NewInt32(ctx, event->eventPhase);
  case 4:
    return JS_NewBool(ctx, event->bubbles);
  case 5:
    return JS_NewBool(ctx, event->cancelable);
  case 6:
    return JS_NewBool(ctx, event->defaultPrevented);
  case 7:
    return JS_NewBool(ctx, event->composed);
  case 8:
    return JS_NewBool(ctx, event->isTrusted);
  case 9:
    return JS_NewFloat64(ctx, event->timeStamp);
  default:
    break;
  }

  return JS_UNDEFINED;
}

// CustomEvent特有的方法
static JSValue js_custom_event_get_detail(JSContext *ctx, JSValueConst this_val) {
  Event *event = get_event(this_val);
  if (!event)
    return JS_EXCEPTION;
  return JS_DupValue(ctx, event->detail);
}

// Event原型方法: stopPropagation
static JSValue js_event_stop_propagation(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  Event *event = get_event(this_val);
  if (!event)
    return JS_EXCEPTION;

  event->stopPropagation = true;
  return JS_UNDEFINED;
}

// Event原型方法: stopImmediatePropagation
static JSValue js_event_stop_immediate_propagation(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  Event *event = get_event(this_val);
  if (!event)
    return JS_EXCEPTION;

  event->stopPropagation = true;
  event->stopImmediatePropagation = true;
  return JS_UNDEFINED;
}

// Event原型方法: preventDefault
static JSValue js_event_prevent_default(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  Event *event = get_event(this_val);
  if (!event)
    return JS_EXCEPTION;

  if (event->cancelable)
    event->defaultPrevented = true;
  return JS_UNDEFINED;
}

static JSValue js_event_target_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
  JSValue obj;
  EventTarget *target;

  // 检查是否使用了 new 关键字
  if (JS_IsUndefined(new_target))
    return JS_ThrowTypeError(ctx, "Constructor EventTarget requires 'new'");

  target = calloc(1, sizeof(EventTarget));
  if (!target)
    return JS_EXCEPTION;

  obj = JS_NewObjectClass(ctx, js_event_target_class_id);
  if (JS_IsException(obj)) {
    free(target);
    return JS_EXCEPTION;
  }

  JS_SetOpaque(obj, target);
  return obj;
}

static void js_event_target_finalizer(JSRuntime *rt, JSValue val) {
  EventTarget *target = JS_GetOpaque(val, js_event_target_class_id);
  if (target) {
    // 释放所有监听器
    EventListener *listener = target->listeners;
    while (listener) {
      EventListener *next = listener->next;
      JS_FreeValueRT(rt, listener->callback);
      if (listener->type)
        free(listener->type);
      free(listener);
      listener = next;
    }
    target->listeners = NULL;
    free(target);
  }
}

// GC 标记函数 - 标记我们对象中引用的 JavaScript 值
static void js_event_target_gc_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark_func) {
  EventTarget *target = JS_GetOpaque(val, js_event_target_class_id);
  if (target) {
    EventListener *listener = target->listeners;
    while (listener) {
      JS_MarkValue(rt, listener->callback, mark_func);
      listener = listener->next;
    }
  }
}

// EventTarget方法: addEventListener
static JSValue js_event_target_add_event_listener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  EventTarget *target = JS_GetOpaque(this_val, js_event_target_class_id);
  if (!target)
    return JS_EXCEPTION;

  // 检查参数
  if (argc < 2 || JS_IsNull(argv[1]) || JS_IsUndefined(argv[1]))
    return JS_UNDEFINED;

  const char *type = JS_ToCString(ctx, argv[0]);
  if (!type)
    return JS_EXCEPTION;

  JSValue callback = argv[1];
  bool capture = false;
  bool once = false;
  bool passive = false;

  // 解析options参数
  if (argc > 2 && !JS_IsUndefined(argv[2])) {
    if (JS_IsBool(argv[2])) {
      capture = JS_ToBool(ctx, argv[2]);
    } else {
      JSValue capture_val = JS_GetPropertyStr(ctx, argv[2], "capture");
      if (!JS_IsException(capture_val))
        capture = JS_ToBool(ctx, capture_val);
      JS_FreeValue(ctx, capture_val);

      JSValue once_val = JS_GetPropertyStr(ctx, argv[2], "once");
      if (!JS_IsException(once_val))
        once = JS_ToBool(ctx, once_val);
      JS_FreeValue(ctx, once_val);

      JSValue passive_val = JS_GetPropertyStr(ctx, argv[2], "passive");
      if (!JS_IsException(passive_val))
        passive = JS_ToBool(ctx, passive_val);
      JS_FreeValue(ctx, passive_val);
    }
  }

  // 检查是否已存在相同的监听器
  EventListener *existing = find_event_listener(target, type, callback, capture);
  if (existing) {
    JS_FreeCString(ctx, type);
    return JS_UNDEFINED;
  }

  // 创建新的监听器
  EventListener *listener = calloc(1, sizeof(EventListener));
  if (!listener) {
    JS_FreeCString(ctx, type);
    return JS_EXCEPTION;
  }

  listener->type = strdup(type);
  listener->callback = JS_DupValue(ctx, callback);
  listener->capture = capture;
  listener->once = once;
  listener->passive = passive;
  listener->removed = false;

  // 添加到监听器链表
  listener->next = target->listeners;
  target->listeners = listener;

  JS_FreeCString(ctx, type);

  return JS_UNDEFINED;
}

// EventTarget方法: removeEventListener
static JSValue js_event_target_remove_event_listener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  EventTarget *target = JS_GetOpaque(this_val, js_event_target_class_id);
  if (!target)
    return JS_EXCEPTION;

  // 检查参数
  if (argc < 2 || JS_IsNull(argv[1]) || JS_IsUndefined(argv[1]))
    return JS_UNDEFINED;

  const char *type = JS_ToCString(ctx, argv[0]);
  if (!type)
    return JS_EXCEPTION;

  JSValue callback = argv[1];
  bool capture = false;

  // 解析capture选项
  if (argc > 2 && !JS_IsUndefined(argv[2])) {
    if (JS_IsBool(argv[2])) {
      capture = JS_ToBool(ctx, argv[2]);
    } else {
      JSValue capture_val = JS_GetPropertyStr(ctx, argv[2], "capture");
      if (!JS_IsException(capture_val))
        capture = JS_ToBool(ctx, capture_val);
      JS_FreeValue(ctx, capture_val);
    }
  }

  // 查找并移除监听器
  EventListener **pprev = &target->listeners;
  EventListener *listener = target->listeners;

  while (listener) {
    if (listener->capture == capture && !strcmp(listener->type, type) && JS_StrictEq(ctx, listener->callback, callback)) {

      *pprev = listener->next;
      JS_FreeValue(ctx, listener->callback);
      free(listener->type);
      free(listener);
      break;
    }
    pprev = &listener->next;
    listener = listener->next;
  }

  JS_FreeCString(ctx, type);
  return JS_UNDEFINED;
}

// 调用事件监听器
static bool invoke_event_listeners(JSContext *ctx, Event *event, JSValue js_event, EventTarget *target, const char *type) {
  bool prevented = false;
  EventListener *listener = target->listeners;
  EventListener *next;

  // 创建临时列表，以防在处理过程中监听器被修改
  EventListener *temp_list = NULL;
  while (listener) {
    if (!listener->removed && !strcmp(listener->type, type)) {
      EventListener *temp = calloc(1, sizeof(EventListener));
      if (temp) {
        temp->callback = JS_DupValue(ctx, listener->callback);
        temp->capture = listener->capture;
        temp->passive = listener->passive;
        temp->once = listener->once;
        temp->next = temp_list;
        temp_list = temp;

        // 如果是一次性监听器，将其标记为已移除
        if (listener->once)
          listener->removed = true;
      }
    }
    listener = listener->next;
  }

  // 目标阶段
  event->eventPhase = EVENT_AT_TARGET;
  listener = temp_list;
  while (listener && !event->stopImmediatePropagation) {
    JSValueConst args[1];
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue result;

    // 设置事件当前目标
    event->currentTarget = JS_DupValue(ctx, event->target);

    args[0] = js_event;

    // 调用回调函数
    if (JS_IsFunction(ctx, listener->callback)) {
      result = JS_Call(ctx, listener->callback, global_obj, 1, args);
    } else {
      JSValue handleEvent = JS_GetPropertyStr(ctx, listener->callback, "handleEvent");
      if (JS_IsFunction(ctx, handleEvent)) {
        result = JS_Call(ctx, handleEvent, listener->callback, 1, args);
        JS_FreeValue(ctx, handleEvent);
      } else {
        result = JS_UNDEFINED;
        JS_FreeValue(ctx, handleEvent);
      }
    }

    if (JS_IsException(result)) {
      JS_GetException(ctx); // 清除异常
    }

    JS_FreeValue(ctx, result);
    JS_FreeValue(ctx, global_obj);
    JS_FreeValue(ctx, event->currentTarget);
    event->currentTarget = JS_NULL;

    // 检查是否阻止了默认行为
    if (event->defaultPrevented)
      prevented = true;

    // 如果设置了立即停止传播，则退出循环
    if (event->stopImmediatePropagation)
      break;

    listener = listener->next;
  }

  // 清理临时列表
  listener = temp_list;
  while (listener) {
    next = listener->next;
    JS_FreeValue(ctx, listener->callback);
    free(listener);
    listener = next;
  }

  // 清理事件状态
  event->eventPhase = EVENT_NONE;
  event->stopPropagation = false;
  event->stopImmediatePropagation = false;

  return !prevented;
}

// EventTarget方法: dispatchEvent
static JSValue js_event_target_dispatch_event(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  EventTarget *target = JS_GetOpaque(this_val, js_event_target_class_id);
  if (!target)
    return JS_EXCEPTION;

  if (argc < 1)
    return JS_ThrowTypeError(ctx, "Missing event parameter");

  Event *event = get_event(argv[0]);
  if (!event)
    return JS_ThrowTypeError(ctx, "Invalid event object");

  // 设置事件目标
  if (!JS_IsNull(event->target) && !JS_IsUndefined(event->target)) {
    JS_FreeValue(ctx, event->target); // 释放之前的值，如果有的话
  }
  event->target = JS_DupValue(ctx, this_val);

  JSValue js_event = JS_DupValue(ctx, argv[0]);
  // 调用事件监听器
  bool result = invoke_event_listeners(ctx, event, js_event, target, event->type);

  JS_FreeValue(ctx, js_event);
  // JS_FreeValue(ctx, event->target);

  return JS_NewBool(ctx, result);
}

static JSClassDef js_event_class_def = {
    "Event",
    .finalizer = js_event_finalizer,
    .gc_mark = js_event_gc_mark,
};

static JSClassDef js_custom_event_class_def = {
    "CustomEvent",
    .finalizer = js_event_finalizer,
    .gc_mark = js_event_gc_mark,
};

static JSClassDef js_event_target_class_def = {
    "EventTarget",
    .finalizer = js_event_target_finalizer,
    .gc_mark = js_event_target_gc_mark,
};

static JSCFunctionListEntry js_event_class_props[] = {
    JS_PROP_INT32_DEF("NONE", EVENT_NONE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("CAPTURING_PHASE", EVENT_CAPTURING_PHASE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("AT_TARGET", EVENT_AT_TARGET, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("BUBBLING_PHASE", EVENT_BUBBLING_PHASE, JS_PROP_ENUMERABLE),
};

static JSCFunctionListEntry js_event_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("type", js_event_get_property, NULL, 0),
    JS_CGETSET_MAGIC_DEF("target", js_event_get_property, NULL, 1),
    JS_CGETSET_MAGIC_DEF("currentTarget", js_event_get_property, NULL, 2),
    JS_CGETSET_MAGIC_DEF("eventPhase", js_event_get_property, NULL, 3),
    JS_CGETSET_MAGIC_DEF("bubbles", js_event_get_property, NULL, 4),
    JS_CGETSET_MAGIC_DEF("cancelable", js_event_get_property, NULL, 5),
    JS_CGETSET_MAGIC_DEF("defaultPrevented", js_event_get_property, NULL, 6),
    JS_CGETSET_MAGIC_DEF("composed", js_event_get_property, NULL, 7),
    JS_CGETSET_MAGIC_DEF("isTrusted", js_event_get_property, NULL, 8),
    JS_CGETSET_MAGIC_DEF("timeStamp", js_event_get_property, NULL, 9),
    JS_CFUNC_DEF("stopPropagation", 0, js_event_stop_propagation),
    JS_CFUNC_DEF("stopImmediatePropagation", 0, js_event_stop_immediate_propagation),
    JS_CFUNC_DEF("preventDefault", 0, js_event_prevent_default),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Event", JS_PROP_CONFIGURABLE),
};

static JSCFunctionListEntry js_custom_event_proto_funcs[] = {
    JS_CGETSET_DEF("detail", js_custom_event_get_detail, NULL),
};

static JSCFunctionListEntry js_event_target_proto_funcs[] = {
    JS_CFUNC_DEF("addEventListener", 0, js_event_target_add_event_listener),
    JS_CFUNC_DEF("removeEventListener", 0, js_event_target_remove_event_listener),
    JS_CFUNC_DEF("dispatchEvent", 0, js_event_target_dispatch_event),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "EventTarget", JS_PROP_CONFIGURABLE),
};

void js_init_event(JSContext *ctx) {
  JSValue event_proto, event_class;
  JSValue custom_event_proto, custom_event_class;
  JSValue event_target_proto, event_target_class;

  // ******************* Event *******************
  // 创建Event类
  JS_NewClassID(&js_event_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_event_class_id, &js_event_class_def);
  // 创建Event原型
  event_proto = JS_NewObject(ctx);
  // 为原型对象添加方法
  JS_SetPropertyFunctionList(ctx, event_proto, js_event_proto_funcs, countof(js_event_proto_funcs));
  // Create constructor，创建构造函数
  event_class = JS_NewCFunctionMagic(ctx, js_event_constructor, "Event", 1, JS_CFUNC_constructor_magic, 0);
  JS_SetPropertyFunctionList(ctx, event_class, js_event_class_props, countof(js_event_class_props));
  // 设置Event原型和类
  JS_SetConstructor(ctx, event_class, event_proto);
  JS_SetClassProto(ctx, js_event_class_id, event_proto);

  // ******************* CustomEvent *******************
  // 创建CustomEvent类
  JS_NewClassID(&js_custom_event_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_custom_event_class_id, &js_custom_event_class_def);
  // 创建CustomEvent原型
  custom_event_proto = JS_NewObjectProto(ctx, event_proto); // 继承自Event.prototype
  JS_SetPropertyFunctionList(ctx, custom_event_proto, js_custom_event_proto_funcs, countof(js_custom_event_proto_funcs));
  custom_event_class = JS_NewCFunctionMagic(ctx, js_event_constructor, "CustomEvent", 1, JS_CFUNC_constructor_magic, 1);
  JS_SetPropertyFunctionList(ctx, custom_event_class, js_event_class_props, countof(js_event_class_props));
  // 设置CustomEvent原型和类
  JS_SetConstructor(ctx, custom_event_class, custom_event_proto);
  JS_SetClassProto(ctx, js_custom_event_class_id, custom_event_proto);

  // ******************* EventTarget *******************
  // 创建EventTarget类
  JS_NewClassID(&js_event_target_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_event_target_class_id, &js_event_target_class_def);
  // 创建EventTarget原型
  event_target_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, event_target_proto, js_event_target_proto_funcs, countof(js_event_target_proto_funcs));
  // Create constructor，创建构造函数
  event_target_class = JS_NewCFunction2(ctx, js_event_target_constructor, "EventTarget", 0, JS_CFUNC_constructor, 0);
  // 为原型对象添加方法
  // 设置CustomEvent原型和类
  JS_SetConstructor(ctx, event_target_class, event_target_proto);
  JS_SetClassProto(ctx, js_event_target_class_id, event_target_proto);

  JSValue global_obj = JS_GetGlobalObject(ctx);

  // Set the class as a global property
  JS_SetPropertyStr(ctx, global_obj, "Event", event_class);
  JS_SetPropertyStr(ctx, global_obj, "CustomEvent", custom_event_class);
  JS_SetPropertyStr(ctx, global_obj, "EventTarget", event_target_class);

  JS_FreeValue(ctx, global_obj);
}
