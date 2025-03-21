#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <quickjs.h>

#include "headers.h"
// #include "log.h"

static JSClassID js_headers_class_id = 0;
static JSValue headers_constructor(JSContext *ctx, JSValueConst new_target,
                                   int argc, JSValueConst *argv);
static void js_headers_finalizer(JSRuntime *rt, JSValue val);

// 工具函数：转换为小写
static char *to_lowercase(const char *str) {
  if (!str)
    return NULL;

  char *result = strdup(str);
  for (int i = 0; result[i]; i++) {
    result[i] = tolower(result[i]);
  }
  return result;
}

// 检查字符串是否为有效的 header 名称
static bool is_valid_header_name(const char *name) {
  if (!name || strlen(name) == 0)
    return false;

  for (int i = 0; name[i]; i++) {
    char c = name[i];
    if (c <= 32 || c >= 127 || c == ':')
      return false;
  }
  return true;
}

// 检查字符串是否为有效的 header 值
static bool is_valid_header_value(const char *value) {
  if (!value)
    return false;

  for (int i = 0; value[i]; i++) {
    unsigned char c = value[i];
    if ((c < 32 && c != 9) || c == 127)
      return false;
  }
  return true;
}

// 标准化 header 值
static char *normalize_value(const char *value) {
  if (!value)
    return NULL;

  size_t len = strlen(value);
  char *result = malloc(len + 1);
  if (!result)
    return NULL;

  size_t j = 0;
  for (size_t i = 0; i < len; i++) {
    if (value[i] == '\r' || value[i] == '\n') {
      continue;
    }
    result[j++] = value[i];
  }
  result[j] = '\0';

  return result;
}

// 检查是否为禁止的请求头
bool is_forbidden_request_header(const char *name) {
  static const char *forbidden[] = {"accept-charset",
                                    "accept-encoding",
                                    "access-control-request-headers",
                                    "access-control-request-method",
                                    "connection",
                                    "content-length",
                                    "cookie",
                                    "cookie2",
                                    "date",
                                    "dnt",
                                    "expect",
                                    "host",
                                    "keep-alive",
                                    "origin",
                                    "referer",
                                    "te",
                                    "trailer",
                                    "transfer-encoding",
                                    "upgrade",
                                    "via",
                                    NULL};

  char *lower_name = to_lowercase(name);
  if (!lower_name)
    return true;

  for (int i = 0; forbidden[i]; i++) {
    if (strcmp(lower_name, forbidden[i]) == 0) {
      free(lower_name);
      return true;
    }
  }

  free(lower_name);
  return false;
}

// 检查是否为禁止的响应头
bool is_forbidden_response_header(const char *name) {
  static const char *forbidden[] = {"set-cookie", "set-cookie2", NULL};

  char *lower_name = to_lowercase(name);
  if (!lower_name)
    return true;

  for (int i = 0; forbidden[i]; i++) {
    if (strcmp(lower_name, forbidden[i]) == 0) {
      free(lower_name);
      return true;
    }
  }

  free(lower_name);
  return false;
}

// 检查是否为 no-CORS 安全的请求头
bool is_no_cors_safelisted_request_header(const char *name, const char *value) {
  static const char *safelisted[] = {"accept", "accept-language",
                                     "content-language", "content-type", NULL};

  char *lower_name = to_lowercase(name);
  if (!lower_name)
    return false;

  bool is_safelisted = false;
  for (int i = 0; safelisted[i]; i++) {
    if (strcmp(lower_name, safelisted[i]) == 0) {
      is_safelisted = true;
      break;
    }
  }

  free(lower_name);

  if (!is_safelisted)
    return false;

  // 对 content-type 进行特殊处理
  if (strcasecmp(name, "content-type") == 0) {
    static const char *allowed_types[] = {"application/x-www-form-urlencoded",
                                          "multipart/form-data", "text/plain",
                                          NULL};

    for (int i = 0; allowed_types[i]; i++) {
      if (strcasecmp(value, allowed_types[i]) == 0) {
        return true;
      }
    }
    return false;
  }

  return true;
}

// 检查是否为特权 no-CORS 请求头
bool is_privileged_no_cors_request_header(const char *name) {
  static const char *privileged[] = {"range", NULL};

  char *lower_name = to_lowercase(name);
  if (!lower_name)
    return false;

  for (int i = 0; privileged[i]; i++) {
    if (strcmp(lower_name, privileged[i]) == 0) {
      free(lower_name);
      return true;
    }
  }

  free(lower_name);
  return false;
}

// 验证 header
bool validate_header(Headers *headers, const char *name, const char *value) {
  if (!is_valid_header_name(name) || !is_valid_header_value(value)) {
    return false;
  }

  if (headers->guard == GUARD_IMMUTABLE) {
    return false;
  }

  if (headers->guard == GUARD_REQUEST && is_forbidden_request_header(name)) {
    return false;
  }

  if (headers->guard == GUARD_RESPONSE && is_forbidden_response_header(name)) {
    return false;
  }

  return true;
}

// 创建 Headers 对象
Headers *headers_new() {
  Headers *headers = malloc(sizeof(Headers));
  if (!headers)
    return NULL;

  headers->headerList = NULL;
  headers->guard = GUARD_NONE;

  return headers;
}

// 查找 header
HeaderNode *find_header(Headers *headers, const char *name) {
  HeaderNode *current = headers->headerList;
  while (current) {
    if (strcasecmp(current->name, name) == 0) {
      return current;
    }
    current = current->next;
  }
  return NULL;
}

static void headers_append_node_at_last(Headers *headers, HeaderNode *node) {
  // If the list is empty, make the new node the first one
  if (headers->headerList == NULL) {
    headers->headerList = node;
  } else {
    // Otherwise, traverse to find the last node and append
    HeaderNode *current = headers->headerList;
    while (current->next != NULL) {
      current = current->next;
    }
    current->next = node;
  }
}

// 获取 header 值
char *headers_get(Headers *headers, const char *name) {
  if (!is_valid_header_name(name)) {
    return NULL; // 应该抛出 TypeError
  }

  HeaderNode *node = find_header(headers, name);
  if (!node) {
    return NULL;
  }

  return strdup(node->value);
}

// 检查 header 是否存在
bool headers_has(Headers *headers, const char *name) {
  if (!is_valid_header_name(name)) {
    return false; // 应该抛出 TypeError
  }

  return find_header(headers, name) != NULL;
}

// 删除特权 no-CORS 请求头
void remove_privileged_no_cors_request_headers(Headers *headers) {
  HeaderNode *current = headers->headerList;
  HeaderNode *prev = NULL;

  while (current) {
    if (is_privileged_no_cors_request_header(current->name)) {
      HeaderNode *to_delete = current;

      if (prev) {
        prev->next = current->next;
        current = current->next;
      } else {
        headers->headerList = current->next;
        current = headers->headerList;
      }

      free(to_delete->name);
      free(to_delete->value);
      free(to_delete);
    } else {
      prev = current;
      current = current->next;
    }
  }
}

// 添加 header
int headers_append(Headers *headers, const char *name, const char *value) {
  char *normalized = normalize_value(value);
  if (!normalized)
    return 1;

  if (!validate_header(headers, name, normalized)) {
    free(normalized);
    return 1;
  }

  if (headers->guard == GUARD_REQUEST_NO_CORS) {
    HeaderNode *existing = find_header(headers, name);
    char *temp_value = NULL;

    if (existing) {
      size_t len =
          strlen(existing->value) + strlen(normalized) + 3; // +3 for ", "
      temp_value = malloc(len);
      if (!temp_value) {
        free(normalized);
        return 1;
      }

      sprintf(temp_value, "%s, %s", existing->value, normalized);

      if (!is_no_cors_safelisted_request_header(name, temp_value)) {
        free(temp_value);
        free(normalized);
        return 1;
      }

      free(temp_value);
    } else {
      if (!is_no_cors_safelisted_request_header(name, normalized)) {
        free(normalized);
        return 1;
      }
    }
  }

  HeaderNode *new_node = malloc(sizeof(HeaderNode));
  if (!new_node) {
    free(normalized);
    return 1;
  }

  new_node->name = strdup(name);
  new_node->value = normalized;
  new_node->next = NULL; // The new node is the last one

  headers_append_node_at_last(headers, new_node);

  if (headers->guard == GUARD_REQUEST_NO_CORS) {
    remove_privileged_no_cors_request_headers(headers);
  }

  return 0;
}

// 删除 header
int headers_delete(Headers *headers, const char *name) {
  if (!validate_header(headers, name, "")) {
    return 1;
  }

  if (headers->guard == GUARD_REQUEST_NO_CORS &&
      !is_no_cors_safelisted_request_header(name, "") &&
      !is_privileged_no_cors_request_header(name)) {
    return 1;
  }

  HeaderNode *current = headers->headerList;
  HeaderNode *prev = NULL;

  while (current) {
    if (strcasecmp(current->name, name) == 0) {
      if (prev) {
        prev->next = current->next;
      } else {
        headers->headerList = current->next;
      }

      free(current->name);
      free(current->value);
      free(current);

      if (headers->guard == GUARD_REQUEST_NO_CORS) {
        remove_privileged_no_cors_request_headers(headers);
      }

      return 0;
    }

    prev = current;
    current = current->next;
  }

  return 0;
}

// 设置 header
int headers_set(Headers *headers, const char *name, const char *value) {
  char *normalized = normalize_value(value);
  if (!normalized)
    return 1;

  if (!validate_header(headers, name, normalized)) {
    free(normalized);
    return 1;
  }

  if (headers->guard == GUARD_REQUEST_NO_CORS &&
      !is_no_cors_safelisted_request_header(name, normalized)) {
    free(normalized);
    return 1;
  }

  HeaderNode *current = headers->headerList;
  HeaderNode *prev = NULL;
  bool existing = false;

  while (current) {
    if (strcasecmp(current->name, name) == 0) {
      if (!existing) {
        existing = true;
        free(current->value);
        current->value = normalized;

        prev = current;
        current = current->next;
      } else {
        if (prev) {
          prev->next = current->next;
        } else {
          headers->headerList = current->next;
        }

        free(current->name);
        free(current->value);
        free(current);
        if (prev) {
          current = prev->next;
        } else {
          current = headers->headerList;
        }
      }
    } else {
      prev = current;
      current = current->next;
    }
  }

  if (!existing) {
    HeaderNode *new_node = malloc(sizeof(HeaderNode));
    if (!new_node) {
      free(normalized);
      return 1;
    }

    new_node->name = strdup(name);
    new_node->value = normalized;
    new_node->next = NULL;

    headers_append_node_at_last(headers, new_node);
  }

  if (headers->guard == GUARD_REQUEST_NO_CORS) {
    remove_privileged_no_cors_request_headers(headers);
  }

  return 0;
}

// 获取所有 Set-Cookie 头
char **headers_get_set_cookie(Headers *headers, int *count) {
  *count = 0;
  HeaderNode *current = headers->headerList;

  // 首先计算 Set-Cookie 头的数量
  while (current) {
    if (strcasecmp(current->name, "Set-Cookie") == 0) {
      (*count)++;
    }
    current = current->next;
  }

  if (*count == 0) {
    return NULL;
  }

  // 分配内存并收集所有 Set-Cookie 值
  char **cookies = malloc(sizeof(char *) * (*count));
  if (!cookies) {
    *count = 0;
    return NULL;
  }

  current = headers->headerList;
  int index = 0;

  while (current && index < *count) {
    if (strcasecmp(current->name, "Set-Cookie") == 0) {
      cookies[index++] = strdup(current->value);
    }
    current = current->next;
  }

  return cookies;
}

// 填充 Headers 对象
void headers_fill(Headers *headers, const char ***init, int pairs_count) {
  for (int i = 0; i < pairs_count; i++) {
    const char *name = init[i][0];
    const char *value = init[i][1];
    headers_append(headers, name, value);
  }
}

// 清理 Headers 对象
void headers_free(Headers *headers) {
  HeaderNode *current = headers->headerList;
  while (current) {
    HeaderNode *next = current->next;
    free(current->name);
    free(current->value);
    free(current);
    current = next;
  }
  free(headers);
}

// 创建迭代器
HeadersIterator *headers_iterator_new(Headers *headers) {
  HeadersIterator *iterator = malloc(sizeof(HeadersIterator));
  if (!iterator)
    return NULL;

  iterator->current = headers->headerList;
  return iterator;
}

// 获取下一个键值对
bool headers_iterator_next(HeadersIterator *iterator, char **name,
                           char **value) {
  if (!iterator->current) {
    return false;
  }

  *name = strdup(iterator->current->name);
  *value = strdup(iterator->current->value);
  iterator->current = iterator->current->next;

  return true;
}

// 清理迭代器
void headers_iterator_free(HeadersIterator *iterator) { free(iterator); }

// 从 JS 值中获取 Headers 数据
static Headers *get_headers(JSContext *ctx, JSValueConst this_val) {
  return JS_GetOpaque2(ctx, this_val, js_headers_class_id);
}

// Headers.prototype.append 方法
static JSValue js_headers_append(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
  Headers *headers = get_headers(ctx, this_val);
  if (!headers)
    return JS_EXCEPTION;

  if (argc < 2)
    return JS_ThrowTypeError(ctx, "append requires at least 2 arguments");

  const char *name = JS_ToCString(ctx, argv[0]);

  if (!name)
    return JS_EXCEPTION;

  const char *value = JS_ToCString(ctx, argv[1]);
  if (!value) {
    JS_FreeCString(ctx, name);
    return JS_EXCEPTION;
  }

  int ret = headers_append(headers, name, value);
  JS_FreeCString(ctx, name);
  JS_FreeCString(ctx, value);

  if (ret != 0) {
    return JS_ThrowOutOfMemory(ctx);
  }

  return JS_UNDEFINED;
}

// Headers.prototype.delete 方法
static JSValue js_headers_delete(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
  Headers *headers = get_headers(ctx, this_val);
  if (!headers)
    return JS_EXCEPTION;

  if (argc < 1)
    return JS_ThrowTypeError(ctx, "delete requires at least 1 argument");

  const char *name = JS_ToCString(ctx, argv[0]);
  if (!name)
    return JS_EXCEPTION;

  headers_delete(headers, name);

  JS_FreeCString(ctx, name);

  return JS_UNDEFINED;
}

// Headers.prototype.get 方法
static JSValue js_headers_get(JSContext *ctx, JSValueConst this_val, int argc,
                              JSValueConst *argv) {
  Headers *headers = get_headers(ctx, this_val);
  if (!headers)
    return JS_EXCEPTION;

  if (argc < 1)
    return JS_ThrowTypeError(ctx, "get requires at least 1 argument");

  const char *name = JS_ToCString(ctx, argv[0]);
  if (!name)
    return JS_EXCEPTION;

  if (!is_valid_header_name(name)) {
    JS_FreeCString(ctx, name);
    return JS_ThrowTypeError(ctx, "Invalid header name");
  }

  HeaderNode *current = headers->headerList;
  char *result = NULL;
  size_t result_len = 0;
  bool found = false;

  while (current) {
    if (strcasecmp(current->name, name) == 0) {
      found = true;
      if (result == NULL) {
        result = strdup(current->value);
        if (!result) {
          JS_FreeCString(ctx, name);
          return JS_ThrowOutOfMemory(ctx);
        }
        result_len = strlen(result);
      } else {
        // Append ", " and the value
        size_t value_len = strlen(current->value);
        char *new_result =
            realloc(result, result_len + 2 + value_len +
                                1); // +2 for ", " and +1 for null terminator

        if (!new_result) {
          free(result);
          JS_FreeCString(ctx, name);
          return JS_ThrowOutOfMemory(ctx);
        }

        result = new_result;
        strcat(result + result_len, ", ");
        strcat(result + result_len + 2, current->value);
        result_len += 2 + value_len;
      }
    }
    current = current->next;
  }

  JS_FreeCString(ctx, name);

  if (!found) {
    return JS_NULL;
  }

  JSValue ret = JS_NewString(ctx, result);
  free(result);
  return ret;
}

// Headers.prototype.has 方法
static JSValue js_headers_has(JSContext *ctx, JSValueConst this_val, int argc,
                              JSValueConst *argv) {
  Headers *headers = get_headers(ctx, this_val);
  if (!headers)
    return JS_EXCEPTION;

  if (argc < 1)
    return JS_ThrowTypeError(ctx, "has requires at least 1 argument");

  const char *name = JS_ToCString(ctx, argv[0]);
  if (!name)
    return JS_EXCEPTION;

  if (!is_valid_header_name(name)) {
    JS_FreeCString(ctx, name);
    return JS_ThrowTypeError(ctx, "Invalid header name");
  }

  bool has_node = headers_has(headers, name);
  JS_FreeCString(ctx, name);

  return JS_NewBool(ctx, has_node);
}

// Headers.prototype.set 方法
static JSValue js_headers_set(JSContext *ctx, JSValueConst this_val, int argc,
                              JSValueConst *argv) {
  Headers *headers = get_headers(ctx, this_val);
  if (!headers)
    return JS_EXCEPTION;

  if (argc < 2)
    return JS_ThrowTypeError(ctx, "set requires at least 2 arguments");

  const char *name = JS_ToCString(ctx, argv[0]);
  if (!name)
    return JS_EXCEPTION;

  const char *value = JS_ToCString(ctx, argv[1]);
  if (!value) {
    JS_FreeCString(ctx, name);
    return JS_EXCEPTION;
  }

  int ret = headers_set(headers, name, value);
  JS_FreeCString(ctx, name);
  JS_FreeCString(ctx, value);

  if (ret != 0) {
    return JS_ThrowOutOfMemory(ctx);
  }

  return JS_UNDEFINED;
}

// 处理初始化数据
static bool fill_headers_from_init(JSContext *ctx, Headers *headers,
                                   JSValueConst init) {
  if (JS_IsUndefined(init) || JS_IsNull(init))
    return true;

  // 检查是否为数组
  if (JS_IsArray(ctx, init)) {
    JSValue lengthVal = JS_GetPropertyStr(ctx, init, "length");
    if (JS_IsException(lengthVal))
      return false;

    int64_t length;
    if (JS_ToInt64(ctx, &length, lengthVal)) {
      JS_FreeValue(ctx, lengthVal);
      return false;
    }
    JS_FreeValue(ctx, lengthVal);

    for (int64_t i = 0; i < length; i++) {
      JSValue pair = JS_GetPropertyUint32(ctx, init, i);
      if (JS_IsException(pair))
        return false;

      JSValue pairLengthVal = JS_GetPropertyStr(ctx, pair, "length");
      if (JS_IsException(pairLengthVal)) {
        JS_FreeValue(ctx, pair);
        return false;
      }

      int64_t pairLength;
      if (JS_ToInt64(ctx, &pairLength, pairLengthVal)) {
        JS_FreeValue(ctx, pairLengthVal);
        JS_FreeValue(ctx, pair);
        return false;
      }
      JS_FreeValue(ctx, pairLengthVal);

      if (pairLength != 2) {
        JS_FreeValue(ctx, pair);
        JS_ThrowTypeError(ctx, "Header pair must have exactly 2 elements");
        return false;
      }

      JSValue nameVal = JS_GetPropertyUint32(ctx, pair, 0);
      JSValue valueVal = JS_GetPropertyUint32(ctx, pair, 1);

      if (JS_IsException(nameVal) || JS_IsException(valueVal)) {
        JS_FreeValue(ctx, pair);
        JS_FreeValue(ctx, nameVal);
        JS_FreeValue(ctx, valueVal);
        return false;
      }

      const char *name = JS_ToCString(ctx, nameVal);
      const char *value = JS_ToCString(ctx, valueVal);

      JS_FreeValue(ctx, nameVal);
      JS_FreeValue(ctx, valueVal);
      JS_FreeValue(ctx, pair);

      if (!name || !value) {
        if (name)
          JS_FreeCString(ctx, name);
        if (value)
          JS_FreeCString(ctx, value);
        return false;
      }

      char *normalized = normalize_value(value);
      JS_FreeCString(ctx, value);

      if (!normalized) {
        JS_FreeCString(ctx, name);
        return false;
      }

      if (validate_header(headers, name, normalized)) {
        HeaderNode *new_node = malloc(sizeof(HeaderNode));
        if (!new_node) {
          JS_FreeCString(ctx, name);
          free(normalized);
          return false;
        }

        new_node->name = strdup(name);
        if (!new_node->name) {
          JS_FreeCString(ctx, name);
          free(normalized);
          free(new_node);
          return false;
        }

        new_node->value = normalized;
        new_node->next = headers->headerList;
        headers->headerList = new_node;
      } else {
        JS_FreeCString(ctx, name);
        free(normalized);
      }
    }
  } else {
    // 假设是对象
    JSPropertyEnum *atoms = NULL;
    uint32_t len = 0;

    if (JS_GetOwnPropertyNames(ctx, &atoms, &len, init, JS_GPN_STRING_MASK)) {
      return false;
    }

    for (uint32_t i = 0; i < len; i++) {
      JSValue nameVal = JS_AtomToString(ctx, atoms[i].atom);
      if (JS_IsException(nameVal)) {
        for (uint32_t j = 0; j < len; j++) {
          JS_FreeAtom(ctx, atoms[j].atom);
        }
        js_free(ctx, atoms);
        return false;
      }

      JSValue valueVal = JS_GetProperty(ctx, init, atoms[i].atom);
      if (JS_IsException(valueVal)) {
        JS_FreeValue(ctx, nameVal);
        for (uint32_t j = 0; j < len; j++) {
          JS_FreeAtom(ctx, atoms[j].atom);
        }
        js_free(ctx, atoms);
        return false;
      }

      const char *name = JS_ToCString(ctx, nameVal);
      const char *value = JS_ToCString(ctx, valueVal);

      JS_FreeValue(ctx, nameVal);
      JS_FreeValue(ctx, valueVal);

      if (!name || !value) {
        if (name)
          JS_FreeCString(ctx, name);
        if (value)
          JS_FreeCString(ctx, value);
        for (uint32_t j = 0; j < len; j++) {
          JS_FreeAtom(ctx, atoms[j].atom);
        }
        js_free(ctx, atoms);
        return false;
      }

      char *normalized = normalize_value(value);
      JS_FreeCString(ctx, value);

      if (!normalized) {
        JS_FreeCString(ctx, name);
        for (uint32_t j = 0; j < len; j++) {
          JS_FreeAtom(ctx, atoms[j].atom);
        }
        js_free(ctx, atoms);
        return false;
      }

      if (validate_header(headers, name, normalized)) {
        HeaderNode *new_node = malloc(sizeof(HeaderNode));
        if (!new_node) {
          JS_FreeCString(ctx, name);
          free(normalized);
          for (uint32_t j = 0; j < len; j++) {
            JS_FreeAtom(ctx, atoms[j].atom);
          }
          js_free(ctx, atoms);
          return false;
        }

        new_node->name = strdup(name);
        if (!new_node->name) {
          JS_FreeCString(ctx, name);
          free(normalized);
          free(new_node);
          for (uint32_t j = 0; j < len; j++) {
            JS_FreeAtom(ctx, atoms[j].atom);
          }
          js_free(ctx, atoms);
          return false;
        }

        new_node->value = normalized;
        new_node->next = headers->headerList;
        headers->headerList = new_node;
      } else {
        JS_FreeCString(ctx, name);
        free(normalized);
      }
    }

    for (uint32_t j = 0; j < len; j++) {
      JS_FreeAtom(ctx, atoms[j].atom);
    }
    js_free(ctx, atoms);
  }

  return true;
}

// static JSValue js_create_headers_iterator(JSContext *ctx, HeaderNode *first)
// {
//   JSValue iterator = JS_NewObjectClass(ctx, js_headers_class_id + 1);
//   if (JS_IsException(iterator))
//     return iterator;

//   HeadersIterator *data = js_mallocz(ctx, sizeof(HeadersIterator));
//   if (!data) {
//     JS_FreeValue(ctx, iterator);
//     return JS_EXCEPTION;
//   }

//   data->current = first;
//   JS_SetOpaque(iterator, data);

//   JSValue iterProto = JS_GetPropertyStr(ctx, iterator, "prototype");
//   if (!JS_IsException(iterProto)) {
//     JS_SetPropertyStr(ctx, iterProto, Symbol_toStringTag,
//                       JS_NewString(ctx, "Headers Iterator"));
//     JS_FreeValue(ctx, iterProto);
//   }

//   return iterator;
// }

// // Headers 对象的 Symbol.iterator 方法
// static JSValue js_headers_iterator(JSContext *ctx, JSValueConst this_val,
//                                    int argc, JSValueConst *argv) {
//   Headers *headers = get_headers(ctx, this_val);
//   if (!headers)
//     return JS_EXCEPTION;

//   return js_create_headers_iterator(ctx, headers->headerList);
// }

// Headers 构造函数
static JSValue headers_constructor(JSContext *ctx, JSValueConst new_target,
                                   int argc, JSValueConst *argv) {
  JSValue obj = JS_UNDEFINED;
  Headers *headers = NULL;

  if (JS_IsUndefined(new_target))
    return JS_ThrowTypeError(ctx, "Constructor Headers requires 'new'");

  obj = JS_NewObjectClass(ctx, js_headers_class_id);
  if (JS_IsException(obj))
    return obj;

  headers = js_mallocz(ctx, sizeof(Headers));
  if (!headers) {
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
  }

  // 初始化 headers 成员
  headers->headerList = NULL;
  headers->guard = GUARD_NONE;

  JS_SetOpaque(obj, headers);

  // 处理初始化参数
  if (argc > 0) {
    if (!fill_headers_from_init(ctx, headers, argv[0])) {
      JS_FreeValue(ctx, obj);
      return JS_EXCEPTION;
    }
  }

  // 通过 prototype 获取方法
  JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if (JS_IsException(proto)) {
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
  }

  JS_SetPrototype(ctx, obj, proto);
  JS_FreeValue(ctx, proto);

  return obj;
}

// 清理 Headers 数据
static void free_headers(Headers *headers) {
  HeaderNode *current = headers->headerList;
  while (current) {
    HeaderNode *next = current->next;
    free(current->name);
    free(current->value);
    free(current);
    current = next;
  }
}

// JS_NewClass 的 finalizer 函数
static void js_headers_finalizer(JSRuntime *rt, JSValue val) {
  Headers *headers = JS_GetOpaque(val, js_headers_class_id);
  if (headers) {
    free_headers(headers);
    js_free_rt(rt, headers);
  }
}

// Headers 类定义
static const JSClassDef js_headers_class = {
    "Headers",
    .finalizer = js_headers_finalizer,
};

// static void js_headers_iterator_finalizer(JSRuntime *rt, JSValue val) {
//   HeadersIteratorData *data = JS_GetOpaque(val, js_headers_class_id + 1);
//   if (data) {
//     js_free_rt(rt, data);
//   }
// }

// static const JSClassDef js_headers_iterator_class = {
//     "HeadersIterator",
//     .finalizer = js_headers_iterator_finalizer,
// };

// 定义 Headers 的方法列表
static const JSCFunctionListEntry js_headers_proto_funcs[] = {
    JS_CFUNC_DEF("append", 2, js_headers_append),
    JS_CFUNC_DEF("delete", 1, js_headers_delete),
    JS_CFUNC_DEF("get", 1, js_headers_get),
    // JS_CFUNC_DEF("getSetCookie", 0, js_headers_get_set_cookie),
    JS_CFUNC_DEF("has", 1, js_headers_has),
    JS_CFUNC_DEF("set", 2, js_headers_set),
    // JS_CFUNC_DEF("forEach", 1, js_headers_foreach),
    // JS_CFUNC_DEF("entries", 0, js_headers_entries),
    // JS_CFUNC_DEF("keys", 0, js_headers_keys),
    // JS_CFUNC_DEF("values", 0, js_headers_values),
    // JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Headers",
    // JS_PROP_CONFIGURABLE), JS_CFUNC_DEF("[Symbol.iterator]", 0,
    // js_headers_iterator),
};

// Initialize the class
void js_init_headers(JSContext *ctx) {
  JSValue headers_proto, headers_class;

  // Initialize the class ID
  JS_NewClassID(&js_headers_class_id);
  // 初始化 Headers 类
  JS_NewClass(JS_GetRuntime(ctx), js_headers_class_id, &js_headers_class);

  // 初始化 Iterator 类
  // JS_NewClassID(&js_headers_class_id + 1);
  // JS_NewClass(JS_GetRuntime(ctx), js_headers_class_id + 1,
  //             &js_headers_iterator_class);

  // 初始化 Keys Iterator 类
  // JS_NewClassID(&js_headers_class_id + 2);
  // JS_NewClass(JS_GetRuntime(ctx), js_headers_class_id + 2,
  //             &js_headers_iterator_class);

  // 初始化 Values Iterator 类
  // JS_NewClassID(&js_headers_class_id + 3);
  // JS_NewClass(JS_GetRuntime(ctx), js_headers_class_id + 3,
  //             &js_headers_iterator_class);

  // 创建 Headers 构造函数
  headers_proto = JS_NewObject(ctx);
  // 为原型对象添加方法
  JS_SetPropertyFunctionList(ctx, headers_proto, js_headers_proto_funcs,
                             countof(js_headers_proto_funcs));

  // Create constructor，创建构造函数
  headers_class = JS_NewCFunction2(ctx, headers_constructor, "Headers", 1,
                                   JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, headers_class, headers_proto);
  // JS_SetPropertyStr(ctx, headers_class, "prototype", headers_proto);
  JS_SetClassProto(ctx, js_headers_class_id, headers_proto);

  // 创建 Headers Iterator 原型
  // JSValue iter_proto = JS_NewObject(ctx);
  // JS_SetPropertyFunctionList(ctx, iter_proto,
  // js_headers_iterator_proto_funcs,
  //                            countof(js_headers_iterator_proto_funcs));
  // JS_SetClassProto(ctx, js_headers_class_id + 1, iter_proto);

  // 创建 Keys Iterator 原型
  // JSValue keys_iter_proto = JS_NewObject(ctx);
  // JS_SetPropertyStr(ctx, keys_iter_proto, Symbol_toStringTag,
  //                   JS_NewString(ctx, "Headers Keys Iterator"));
  // JS_CFUNC_DEF("next", 0, js_headers_keys_iterator_next);
  // JS_SetClassProto(ctx, js_headers_class_id + 2, keys_iter_proto);

  // 创建 Values Iterator 原型
  // JSValue values_iter_proto = JS_NewObject(ctx);
  // JS_SetPropertyStr(ctx, values_iter_proto, Symbol_toStringTag,
  //                   JS_NewString(ctx, "Headers Values Iterator"));
  // JS_CFUNC_DEF("next", 0, js_headers_values_iterator_next);
  // JS_SetClassProto(ctx, js_headers_class_id + 3, values_iter_proto);

  JSValue global_obj = JS_GetGlobalObject(ctx);

  // Set the class as a global property
  JS_SetPropertyStr(ctx, global_obj, "Headers", headers_class);

  JS_FreeValue(ctx, global_obj);
}
