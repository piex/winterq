#include <stdio.h>

#include <quickjs.h>

#include "console.h"

#define countof(x) (sizeof(x) / sizeof((x)[0]))

// ANSI Color
#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_BLUE "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN "\x1b[36m"
#define ANSI_COLOR_RESET "\x1b[0m"

typedef struct {
  const char *prefix;
  const char *color;
} ConsoleLogType;

static const ConsoleLogType LOG_TYPES[] = {
    {NULL, NULL},                // log
    {"INFO", NULL},              // info
    {"WARN", ANSI_COLOR_YELLOW}, // warn
    {"ERROR", ANSI_COLOR_RED},   // error
    {"DEBUG", ANSI_COLOR_BLUE}   // debug
};

enum { LOG_TYPE_LOG = 0, LOG_TYPE_INFO, LOG_TYPE_WARN, LOG_TYPE_ERROR, LOG_TYPE_DEBUG };

static JSValue js_console_print(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int log_type) {
  const ConsoleLogType *type = &LOG_TYPES[log_type];
  const char *str;
  size_t len;
  size_t buffer_size = 1024;
  char buffer[buffer_size];
  size_t offset = 0;

  if (type->color) {
    offset += snprintf(buffer + offset, buffer_size - offset, "%s", type->color);
  }

  if (type->prefix) {
    offset += snprintf(buffer + offset, buffer_size - offset, "%s: ", type->prefix);
  }

  for (int i = 0; i < argc; i++) {
    if (i != 0) {
      offset += snprintf(buffer + offset, buffer_size - offset, " ");
    }

    str = JS_ToCStringLen(ctx, &len, argv[i]);
    if (!str) {
      return JS_EXCEPTION;
    }

    offset += snprintf(buffer + offset, buffer_size - offset, "%s", str);
    JS_FreeCString(ctx, str);

    // Check if buffer is full and needs to be flushed
    if (offset >= buffer_size - 1) {
      fwrite(buffer, 1, offset, stderr);
      offset = 0;
    }
  }

  if (type->color) {
    offset += snprintf(buffer + offset, buffer_size - offset, "%s", ANSI_COLOR_RESET);
  }

  offset += snprintf(buffer + offset, buffer_size - offset, "\n");

  // Flush remaining buffer
  if (offset > 0) {
    fwrite(buffer, 1, offset, stderr);
  }

  return JS_UNDEFINED;
}

// Console method wrappers
static JSValue js_console_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  return js_console_print(ctx, this_val, argc, argv, LOG_TYPE_LOG);
}

static JSValue js_console_info(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  return js_console_print(ctx, this_val, argc, argv, LOG_TYPE_INFO);
}

static JSValue js_console_warn(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  return js_console_print(ctx, this_val, argc, argv, LOG_TYPE_WARN);
}

static JSValue js_console_error(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  return js_console_print(ctx, this_val, argc, argv, LOG_TYPE_ERROR);
}

static JSValue js_console_debug(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  return js_console_print(ctx, this_val, argc, argv, LOG_TYPE_DEBUG);
}

// Console time tracking
typedef struct {
  uint64_t start_time;
  char *label;
} ConsoleTimer;

static JSValue js_console_time(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  // Implementation for console.time
  // Add timer functionality
  return JS_UNDEFINED;
}

static JSValue js_console_timeEnd(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  // Implementation for console.timeEnd
  // Add timer end functionality
  return JS_UNDEFINED;
}

void js_init_console(JSContext *ctx) {
  static const JSCFunctionListEntry console_funcs[] = {
      JS_CFUNC_DEF("log", 1, js_console_log),         JS_CFUNC_DEF("info", 1, js_console_info),   JS_CFUNC_DEF("warn", 1, js_console_warn),
      JS_CFUNC_DEF("error", 1, js_console_error),     JS_CFUNC_DEF("debug", 1, js_console_debug), JS_CFUNC_DEF("time", 1, js_console_time),
      JS_CFUNC_DEF("timeEnd", 1, js_console_timeEnd),
  };

  JSValue global_obj = JS_GetGlobalObject(ctx);
  JSValue console = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, console, console_funcs, countof(console_funcs));
  JS_SetPropertyStr(ctx, global_obj, "console", console);

  JS_FreeValue(ctx, global_obj);
}