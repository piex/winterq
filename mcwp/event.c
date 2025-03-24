#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "quickjs.h"

#include "event.h"

JSClassID js_event_class_id = 0;
JSClassID js_custom_event_class_id = 0;

// Event构造函数
static JSValue js_event_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv, int magic) {
  JSValue obj = JS_UNDEFINED;
  Event *event = NULL;
  const char *type = "";
  bool bubbles = false;
  bool cancelable = false;
  bool composed = false;
  JSValue detail = JS_UNDEFINED;

  if (JS_IsUndefined(new_target))
    return JS_ThrowTypeError(ctx, "Constructor Event requires 'new'");

  if (argc < 1)
    return JS_ThrowTypeError(ctx, "1 argument required, but only 0 present");

  // 创建JS对象
  obj = JS_NewObjectClass(ctx, magic == 0 ? js_event_class_id : js_custom_event_class_id);
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

    JSValue detail = JS_GetPropertyStr(ctx, argv[1], "detail");
    if (!JS_IsException(detail))
      detail = JS_DupValue(ctx, detail);
    JS_FreeValue(ctx, detail);
  }

  // 创建Event对象
  event = calloc(1, sizeof(Event));
  if (!event)
    goto fail;

  event->type = strdup(type);
  event->bubbles = bubbles;
  event->cancelable = cancelable;
  event->composed = composed;
  event->timeStamp = clock();
  event->eventPhase = EVENT_NONE;
  event->target = JS_UNDEFINED;
  event->currentTarget = JS_UNDEFINED;
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
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

// 清理函数
static void js_event_finalizer(JSRuntime *rt, JSValue val) {
  Event *event = JS_GetOpaque(val, JS_VALUE_GET_TAG(val));
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

// static void js_event_target_finalizer(JSRuntime *rt, JSValue val) {
//   EventTargetData *data = JS_GetOpaque(val, JS_VALUE_GET_TAG(val));
//   if (data) {
//     // 释放所有监听器
//     EventListener *listener = data->listeners;
//     while (listener) {
//       EventListener *next = listener->next;
//       JS_FreeValueRT(rt, listener->callback);
//       if (listener->type)
//         free(listener->type);
//       free(listener);
//       listener = next;
//     }
//     free(data);
//   }
// }

static JSClassDef js_event_class_def = {
    "Event",
    .finalizer = js_event_finalizer,
};

static JSClassDef js_custom_event_class_def = {
    "CustomEvent",
    .finalizer = js_event_finalizer,
};

static JSCFunctionListEntry js_event_proto_funcs[] = {};

void js_init_event(JSContext *ctx) {
  JSValue event_proto, event_class;
  JSValue custom_event_proto, custom_event_class;
  // JSValue event_target_proto, event_target_class;

  // 创建Event类
  JS_NewClassID(&js_event_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_event_class_id, &js_event_class_def);
  // 创建Event原型
  event_proto = JS_NewObject(ctx);
  // 为原型对象添加方法
  JS_SetPropertyFunctionList(ctx, event_proto, js_event_proto_funcs, countof(js_event_proto_funcs));
  // Create constructor，创建构造函数
  event_class = JS_NewCFunctionMagic(ctx, js_event_constructor, "Event", 1, JS_CFUNC_constructor_magic, 0);
  // 设置Event原型和类
  JS_SetConstructor(ctx, event_class, event_proto);
  JS_SetClassProto(ctx, js_event_class_id, event_proto);

  // 创建CustomEvent类
  JS_NewClassID(&js_custom_event_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_custom_event_class_id, &js_custom_event_class_def);
  // 创建CustomEvent原型
  custom_event_proto = JS_NewObjectProto(ctx, event_proto); // 继承自Event.prototype
  custom_event_class = JS_NewCFunctionMagic(ctx, js_event_constructor, "CustomEvent", 1, JS_CFUNC_constructor_magic, 1);
  // 设置CustomEvent原型和类
  JS_SetConstructor(ctx, custom_event_class, custom_event_proto);
  JS_SetClassProto(ctx, js_custom_event_class_id, custom_event_proto);

  JSValue global_obj = JS_GetGlobalObject(ctx);

  // Set the class as a global property
  JS_SetPropertyStr(ctx, global_obj, "Event", event_class);
  JS_SetPropertyStr(ctx, global_obj, "CustomEvent", custom_event_class);

  JS_FreeValue(ctx, global_obj);
}
