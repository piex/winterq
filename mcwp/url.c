#include <ctype.h>
#include <stdio.h>

#include "common.h"
#include "url.h"

static JSClassID js_url_class_id = 0;
static JSClassID js_url_search_params_class_id = 0;
static JSClassID js_url_search_params_iterator_class_id = 0;

// 迭代器结构
typedef struct {
  JSValue obj;
  JSIteratorKindEnum kind;

  ParamNode *current;
} URLSearchParamsIterator;

static JSValue js_url_search_params_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv);
static void js_url_search_params_finalizer(JSRuntime *rt, JSValue val);
static void js_url_search_params_iterator_finalizer(JSRuntime *rt, JSValue val);

// 辅助函数：URL 编码
static char *url_encode(const char *str) {
  if (!str)
    return NULL;

  size_t len = strlen(str);
  char *encoded = malloc(len * 3 + 1); // 最坏情况下每个字符都需要编码为 %XX
  if (!encoded)
    return NULL;

  size_t j = 0;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = str[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded[j++] = c;
    } else if (c == ' ') {
      encoded[j++] = '+';
    } else {
      sprintf(encoded + j, "%%%02X", c);
      j += 3;
    }
  }
  encoded[j] = '\0';
  return encoded;
}

// 辅助函数：URL 解码
static char *url_decode(const char *str) {
  if (!str)
    return NULL;

  size_t len = strlen(str);
  char *decoded = malloc(len + 1);
  if (!decoded)
    return NULL;

  size_t j = 0;
  for (size_t i = 0; i < len; i++) {
    if (str[i] == '%' && i + 2 < len) {
      int value;
      sscanf(str + i + 1, "%2x", &value);
      decoded[j++] = value;
      i += 2;
    } else if (str[i] == '+') {
      decoded[j++] = ' ';
    } else {
      decoded[j++] = str[i];
    }
  }
  decoded[j] = '\0';
  return decoded;
}

// 辅助函数：解析 URL
static bool parse_url(JSContext *ctx, const char *url_str, const char *base_url_str, URL *url) {
  // 初始化 URL 结构
  memset(url, 0, sizeof(URL));

  // 简化的 URL 解析器 - 这里是一个基本实现，实际生产需要更复杂的解析逻辑
  char *temp = strdup(url_str);
  char *protocol_end = strstr(temp, "://");

  // 默认初始化
  url->href = strdup(url_str);
  url->protocol = NULL;
  url->hostname = NULL;
  url->host = NULL;
  url->pathname = strdup("/");
  url->search = NULL;
  url->hash = NULL;
  url->username = NULL;
  url->password = NULL;
  url->port = -1;

  if (!protocol_end) {
    free(temp);
    return false;
  }

  *protocol_end = '\0';
  url->protocol = strdup(temp);
  *protocol_end = ':';

  char *path_start = protocol_end + 3;
  char *path_end = strchr(path_start, '/');
  char *hash_start = strchr(path_start, '#');
  char *search_start = strchr(path_start, '?');

  // 处理哈希
  if (hash_start) {
    *hash_start = '\0';
    url->hash = strdup(hash_start + 1);
    *hash_start = '#';
  }

  // 处理查询字符串
  if (search_start && (!hash_start || search_start < hash_start)) {
    *search_start = '\0';
    url->search = strdup(search_start);
    *search_start = '?';
  }

  // 处理路径和主机
  if (!path_end)
    path_end = path_start + strlen(path_start);

  char *host_end = strchr(path_start, '/');
  if (!host_end)
    host_end = path_end;

  // 提取主机名和端口
  char *port_start = strchr(path_start, ':');
  if (port_start && port_start < host_end) {
    *port_start = '\0';
    url->hostname = strdup(path_start);
    url->port = atoi(port_start + 1);
    *port_start = ':';
  } else {
    url->hostname = strndup(path_start, host_end - path_start);
  }

  // 设置主机
  if (url->port != -1) {
    char host_buffer[256];
    snprintf(host_buffer, sizeof(host_buffer), "%s:%d", url->hostname, url->port);
    url->host = strdup(host_buffer);
  } else {
    url->host = strdup(url->hostname);
  }

  // 路径处理
  if (host_end && *host_end == '/') {
    url->pathname = strdup(host_end);
  }

  free(temp);
  return true;
}

// 解析查询字符串
static void parse_query_string(URLSearchParams *params, const char *query) {
  if (!query || !params)
    return;

  // 跳过开头的 '?' 字符（如果有）
  if (*query == '?')
    query++;

  char *query_copy = strdup(query);
  if (!query_copy)
    return;

  char *token = strtok(query_copy, "&");
  while (token) {
    char *equals = strchr(token, '=');
    if (equals) {
      *equals = '\0';
      char *name = url_decode(token);
      char *value = url_decode(equals + 1);

      if (name && value) {
        ParamNode *new_node = malloc(sizeof(ParamNode));
        if (new_node) {
          new_node->name = name;
          new_node->value = value;
          new_node->next = params->paramList;
          params->paramList = new_node;
        } else {
          free(name);
          free(value);
        }
      } else {
        if (name)
          free(name);
        if (value)
          free(value);
      }
    } else {
      // 没有 '=' 的情况，值为空字符串
      char *name = url_decode(token);
      if (name) {
        ParamNode *new_node = malloc(sizeof(ParamNode));
        if (new_node) {
          new_node->name = name;
          new_node->value = strdup("");
          new_node->next = params->paramList;
          params->paramList = new_node;
        } else {
          free(name);
        }
      }
    }

    token = strtok(NULL, "&");
  }

  free(query_copy);

  // 反转链表，保持原始顺序
  ParamNode *prev = NULL;
  ParamNode *current = params->paramList;
  ParamNode *next = NULL;

  while (current) {
    next = current->next;
    current->next = prev;
    prev = current;
    current = next;
  }

  params->paramList = prev;
}

// URL 构造函数
static JSValue js_url_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
  JSValue obj = JS_UNDEFINED;
  URL *url = NULL;
  const char *url_str = NULL;
  const char *base_url_str = NULL;

  if (JS_IsUndefined(new_target))
    return JS_ThrowTypeError(ctx, "Constructor URL requires 'new'");

  if (argc < 1)
    return JS_ThrowTypeError(ctx, "1 argument required, but only 0 present");

  // 创建 JS 对象
  obj = JS_NewObjectClass(ctx, js_url_class_id);
  if (JS_IsException(obj))
    goto fail;

  // 解析 URL 字符串
  url_str = JS_ToCString(ctx, argv[0]);
  if (!url_str)
    goto fail;

  // 处理基础 URL
  if (argc > 1 && !JS_IsUndefined(argv[1])) {
    base_url_str = JS_ToCString(ctx, argv[1]);
  }

  // 分配 URL 结构体
  url = calloc(1, sizeof(URL));
  if (!url)
    goto fail;

  // 解析 URL
  if (!parse_url(ctx, url_str, base_url_str, url)) {
    JS_FreeCString(ctx, url_str);
    if (base_url_str)
      JS_FreeCString(ctx, base_url_str);
    JS_ThrowTypeError(ctx, "Invalid URL");
    goto fail;
  }

  JS_SetOpaque(obj, url);
  JS_FreeCString(ctx, url_str);
  if (base_url_str)
    JS_FreeCString(ctx, base_url_str);
  return obj;

fail:
  if (url_str)
    JS_FreeCString(ctx, url_str);
  if (url)
    free(url);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

// URL 清理函数
static void js_url_finalizer(JSRuntime *rt, JSValue val) {
  URL *url = JS_GetOpaque(val, js_url_class_id);
  if (url) {
    free(url->href);
    free(url->protocol);
    free(url->hostname);
    free(url->host);
    free(url->pathname);
    free(url->search);
    free(url->hash);
    free(url->username);
    free(url->password);
    free(url);
  }
}

// URL getter 方法
static JSValue js_url_get_property(JSContext *ctx, JSValueConst this_val, int magic) {
  URL *url = JS_GetOpaque(this_val, js_url_class_id);
  if (!url)
    return JS_EXCEPTION;

  switch (magic) {
  case 0: // href
    return JS_NewString(ctx, url->href ? url->href : "");
  case 1: // protocol
    return JS_NewString(ctx, url->protocol ? url->protocol : "");
  case 2: // hostname
    return JS_NewString(ctx, url->hostname ? url->hostname : "");
  case 3: // host
    return JS_NewString(ctx, url->host ? url->host : "");
  case 4: // pathname
    return JS_NewString(ctx, url->pathname ? url->pathname : "/");
  case 5: // search
    return JS_NewString(ctx, url->search ? url->search : "");
  case 6: // hash
    return JS_NewString(ctx, url->hash ? url->hash : "");
  case 7: // port
    return url->port != -1 ? JS_NewInt32(ctx, url->port) : JS_NewString(ctx, "");
  case 8: // username
    return JS_NewString(ctx, url->username ? url->username : "");
  case 9: // password
    return JS_NewString(ctx, url->password ? url->password : "");
  case 10: // origin
      ;
    char origin[512];
    snprintf(origin, sizeof(origin), "%s//%s", url->protocol, url->host);
    return JS_NewString(ctx, origin);
  case 11: // searchParams
    return JS_UNDEFINED;
  }

  return JS_UNDEFINED;
}

// 创建 URLSearchParams 对象
URLSearchParams *url_search_params_new() {
  URLSearchParams *params = malloc(sizeof(URLSearchParams));
  if (!params)
    return NULL;

  params->paramList = NULL;

  return params;
}

// 将节点追加到链表末尾
static void url_search_params_append_node(URLSearchParams *params, ParamNode *node) {
  if (!params->paramList) {
    params->paramList = node;
    node->next = NULL;
    return;
  }

  ParamNode *current = params->paramList;
  while (current->next) {
    current = current->next;
  }

  current->next = node;
  node->next = NULL;
}

// 添加参数
int url_search_params_append(URLSearchParams *params, const char *name, const char *value) {
  if (!params || !name)
    return 1;

  ParamNode *new_node = malloc(sizeof(ParamNode));
  if (!new_node)
    return 1;

  new_node->name = strdup(name);
  new_node->value = value ? strdup(value) : strdup("");

  if (!new_node->name || !new_node->value) {
    if (new_node->name)
      free(new_node->name);
    if (new_node->value)
      free(new_node->value);
    free(new_node);
    return 1;
  }

  url_search_params_append_node(params, new_node);

  return 0;
}

// 删除参数
int url_search_params_delete(URLSearchParams *params, const char *name) {
  if (!params || !name)
    return 1;

  ParamNode *current = params->paramList;
  ParamNode *prev = NULL;

  while (current) {
    if (strcmp(current->name, name) == 0) {
      if (prev) {
        prev->next = current->next;
      } else {
        params->paramList = current->next;
      }

      free(current->name);
      free(current->value);
      free(current);

      // 继续查找同名参数
      if (prev) {
        current = prev->next;
      } else {
        current = params->paramList;
      }
    } else {
      prev = current;
      current = current->next;
    }
  }

  return 0;
}

// 获取参数值（第一个匹配的）
char *url_search_params_get(URLSearchParams *params, const char *name) {
  if (!params || !name)
    return NULL;

  ParamNode *current = params->paramList;
  while (current) {
    if (strcmp(current->name, name) == 0) {
      return strdup(current->value);
    }
    current = current->next;
  }

  return NULL;
}

// 获取所有匹配参数值
char **url_search_params_get_all(URLSearchParams *params, const char *name, int *count) {
  if (!params || !name || !count)
    return NULL;

  *count = 0;
  ParamNode *current = params->paramList;

  // 计算匹配数量
  while (current) {
    if (strcmp(current->name, name) == 0) {
      (*count)++;
    }
    current = current->next;
  }

  if (*count == 0)
    return NULL;

  // 分配结果数组
  char **values = malloc(sizeof(char *) * (*count));
  if (!values) {
    *count = 0;
    return NULL;
  }

  // 填充结果数组
  current = params->paramList;
  int index = 0;
  while (current && index < *count) {
    if (strcmp(current->name, name) == 0) {
      values[index++] = strdup(current->value);
    }
    current = current->next;
  }

  return values;
}

// 检查参数是否存在
bool url_search_params_has(URLSearchParams *params, const char *name) {
  if (!params || !name)
    return false;

  ParamNode *current = params->paramList;
  while (current) {
    if (strcmp(current->name, name) == 0) {
      return true;
    }
    current = current->next;
  }

  return false;
}

// 设置参数（删除所有同名参数，然后添加新参数）
int url_search_params_set(URLSearchParams *params, const char *name, const char *value) {
  if (!params || !name)
    return 1;

  bool setted = false;

  ParamNode *current = params->paramList;
  ParamNode *prev = NULL;

  while (current) {
    if (strcmp(current->name, name) == 0) { // 匹配同名参数
      if (!setted) {                        // 匹配到的第一个替换掉值
        current->value = value ? strdup(value) : strdup("");
        setted = true;
        prev = current;
        current = current->next;
      } else { // 匹配到的不是第一个，删除
        prev->next = current->next;

        free(current->name);
        free(current->value);
        free(current);
        current = prev->next;
      }
    } else {
      prev = current;
      current = current->next;
    }
  }

  // 没有同名参数，则添加到末位
  if (setted == false) {
    return url_search_params_append(params, name, value);
  }

  return 0;
}

// 排序参数
void url_search_params_sort(URLSearchParams *params) {
  if (!params || !params->paramList)
    return;

  // 使用简单的冒泡排序
  bool swapped;
  ParamNode *ptr1;
  ParamNode *lptr = NULL;

  do {
    swapped = false;
    ptr1 = params->paramList;

    while (ptr1->next != lptr) {
      if (strcmp(ptr1->name, ptr1->next->name) > 0) {
        // 交换节点数据
        char *temp_name = ptr1->name;
        char *temp_value = ptr1->value;

        ptr1->name = ptr1->next->name;
        ptr1->value = ptr1->next->value;

        ptr1->next->name = temp_name;
        ptr1->next->value = temp_value;

        swapped = true;
      }
      ptr1 = ptr1->next;
    }
    lptr = ptr1;
  } while (swapped);
}

// 获取查询字符串
char *url_search_params_to_string(URLSearchParams *params) {
  if (!params)
    return strdup("");

  // 计算所需的总长度
  size_t total_len = 0;
  ParamNode *current = params->paramList;

  while (current) {
    char *encoded_name = url_encode(current->name);
    char *encoded_value = url_encode(current->value);

    if (encoded_name && encoded_value) {
      total_len += strlen(encoded_name) + strlen(encoded_value) + 2; // +2 for '=' and '&'
    }

    if (encoded_name)
      free(encoded_name);
    if (encoded_value)
      free(encoded_value);

    current = current->next;
  }

  if (total_len == 0)
    return strdup("");

  // 分配内存（减去最后一个 '&'，加上结束符 '\0'）
  char *result = malloc(total_len);
  if (!result)
    return NULL;

  // 构建查询字符串
  result[0] = '\0';
  current = params->paramList;
  size_t pos = 0;

  while (current) {
    char *encoded_name = url_encode(current->name);
    char *encoded_value = url_encode(current->value);

    if (encoded_name && encoded_value) {
      size_t name_len = strlen(encoded_name);
      size_t value_len = strlen(encoded_value);

      // 复制名称
      memcpy(result + pos, encoded_name, name_len);
      pos += name_len;

      // 添加 '='
      result[pos++] = '=';

      // 复制值
      memcpy(result + pos, encoded_value, value_len);
      pos += value_len;

      // 如果不是最后一个参数，添加 '&'
      if (current->next) {
        result[pos++] = '&';
      }
    }

    if (encoded_name)
      free(encoded_name);
    if (encoded_value)
      free(encoded_value);

    current = current->next;
  }

  result[pos] = '\0';

  return result;
}

// 清理 URLSearchParams 对象
void url_search_params_free(URLSearchParams *params) {
  if (!params)
    return;

  ParamNode *current = params->paramList;
  while (current) {
    ParamNode *next = current->next;
    free(current->name);
    free(current->value);
    free(current);
    current = next;
  }

  free(params);
}

// 处理初始化数据
static bool fill_url_search_params_from_init(JSContext *ctx, URLSearchParams *params, JSValueConst init) {
  if (JS_IsUndefined(init) || JS_IsNull(init))
    return true;

  // 如果是字符串，解析为查询字符串
  if (JS_IsString(init)) {
    const char *str = JS_ToCString(ctx, init);
    if (!str)
      return false;

    parse_query_string(params, str);
    JS_FreeCString(ctx, str);
    return true;
  }

  // 如果是 URLSearchParams 对象，复制其参数
  if (JS_GetOpaque(init, js_url_search_params_class_id)) {
    URLSearchParams *other = JS_GetOpaque(init, js_url_search_params_class_id);
    if (!other)
      return false;

    ParamNode *current = other->paramList;
    while (current) {
      url_search_params_append(params, current->name, current->value);
      current = current->next;
    }

    return true;
  }

  // 如果是数组，处理为键值对数组
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
        JS_ThrowTypeError(ctx, "URLSearchParams pair must have exactly 2 elements");
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

      url_search_params_append(params, name, value);

      JS_FreeCString(ctx, name);
      JS_FreeCString(ctx, value);
    }

    return true;
  }

  // 如果是对象，处理为键值对对象
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

    url_search_params_append(params, name, value);

    JS_FreeCString(ctx, name);
    JS_FreeCString(ctx, value);
  }

  for (uint32_t j = 0; j < len; j++) {
    JS_FreeAtom(ctx, atoms[j].atom);
  }
  js_free(ctx, atoms);

  return true;
}

// URLSearchParams 构造函数
static JSValue js_url_search_params_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
  JSValue obj = JS_UNDEFINED;
  URLSearchParams *params = NULL;

  if (JS_IsUndefined(new_target))
    return JS_ThrowTypeError(ctx, "Constructor URLSearchParams requires 'new'");

  // 创建 JS 对象
  obj = JS_NewObjectClass(ctx, js_url_search_params_class_id);
  if (JS_IsException(obj))
    goto fail;

  // 分配 URLSearchParams 结构体
  params = calloc(1, sizeof(URLSearchParams));
  if (!params)
    goto fail;

  // 初始化 params 成员
  params->paramList = NULL;

  JS_SetOpaque(obj, params);

  // 处理初始化参数
  if (argc > 0) {
    if (!fill_url_search_params_from_init(ctx, params, argv[0])) {
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

fail:
  if (params)
    free(params);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

// URLSearchParams 清理函数
static void js_url_search_params_finalizer(JSRuntime *rt, JSValue val) {
  URLSearchParams *params = JS_GetOpaque(val, js_url_search_params_class_id);
  if (params) {
    ParamNode *current = params->paramList;
    while (current) {
      ParamNode *next = current->next;
      free(current->name);
      free(current->value);
      free(current);
      current = next;
    }
    js_free_rt(rt, params);
  }
}

static void js_url_search_params_iterator_finalizer(JSRuntime *rt, JSValue val) {
  URLSearchParamsIterator *it = JS_GetOpaque(val, js_url_search_params_iterator_class_id);

  if (it) {
    JS_FreeValueRT(rt, it->obj);
    js_free_rt(rt, it);
  }
}

static JSValue js_create_url_search_params_iterator_iterator(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  return JS_DupValue(ctx, this_val);
}

static JSValue js_url_search_params_iterator_next(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int *pdone, int magic) {
  URLSearchParamsIterator *it;
  URLSearchParams *usp;

  it = JS_GetOpaque2(ctx, this_val, js_url_search_params_iterator_class_id);
  if (!it) {
    *pdone = false;
    return JS_EXCEPTION;
  }

  if (JS_IsUndefined(it->obj))
    goto done;

  usp = JS_GetOpaque(it->obj, js_url_search_params_class_id);
  if (!usp)
    return JS_EXCEPTION;

  if (!it->current) {
    it->current = usp->paramList;
  } else {
    it->current = it->current->next;
  }

  if (!it->current) {
    /* no more record  */
    it->current = NULL;
    JS_FreeValue(ctx, it->obj);
    it->obj = JS_UNDEFINED;
    goto done;
  }

  *pdone = false;

  if (it->kind == JS_ITERATOR_KIND_KEY) {
    return JS_NewString(ctx, it->current->name);
  } else if (it->kind == JS_ITERATOR_KIND_VALUE) {
    return JS_NewString(ctx, it->current->value);
  } else {
    JSValue result = JS_NewArray(ctx);
    if (JS_IsException(result))
      return JS_EXCEPTION;

    JS_SetPropertyUint32(ctx, result, 0, JS_NewString(ctx, it->current->name));
    JS_SetPropertyUint32(ctx, result, 1, JS_NewString(ctx, it->current->value));

    return result;
  }

done:
  // 迭代结束
  *pdone = true;
  return JS_UNDEFINED;
}

// URLSearchParams.prototype.append 方法
static JSValue js_url_search_params_append(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  URLSearchParams *params = JS_GetOpaque(this_val, js_url_search_params_class_id);
  if (!params)
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

  int ret = url_search_params_append(params, name, value);

  JS_FreeCString(ctx, name);
  JS_FreeCString(ctx, value);

  if (ret != 0) {
    return JS_ThrowOutOfMemory(ctx);
  }

  return JS_UNDEFINED;
}

// URLSearchParams.prototype.delete 方法
static JSValue js_url_search_params_delete(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  URLSearchParams *params = JS_GetOpaque(this_val, js_url_search_params_class_id);
  if (!params)
    return JS_EXCEPTION;

  if (argc < 1)
    return JS_ThrowTypeError(ctx, "delete requires at least 1 argument");

  const char *name = JS_ToCString(ctx, argv[0]);
  if (!name)
    return JS_EXCEPTION;

  url_search_params_delete(params, name);

  JS_FreeCString(ctx, name);

  return JS_UNDEFINED;
}

// URLSearchParams.prototype.get 方法
static JSValue js_url_search_params_get(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  URLSearchParams *params = JS_GetOpaque(this_val, js_url_search_params_class_id);
  if (!params)
    return JS_EXCEPTION;

  if (argc < 1)
    return JS_ThrowTypeError(ctx, "get requires at least 1 argument, but only 0 present.");

  const char *name = JS_ToCString(ctx, argv[0]);
  if (!name)
    return JS_EXCEPTION;

  char *value = url_search_params_get(params, name);

  JS_FreeCString(ctx, name);

  if (!value)
    return JS_NULL;

  JSValue ret = JS_NewString(ctx, value);
  free(value);

  return ret;
}

// URLSearchParams.prototype.getAll 方法
static JSValue js_url_search_params_get_all(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  URLSearchParams *params = JS_GetOpaque(this_val, js_url_search_params_class_id);
  if (!params)
    return JS_EXCEPTION;

  if (argc < 1)
    return JS_ThrowTypeError(ctx, "getAll requires at least 1 argument");

  const char *name = JS_ToCString(ctx, argv[0]);
  if (!name)
    return JS_EXCEPTION;

  int count = 0;
  char **values = url_search_params_get_all(params, name, &count);

  JS_FreeCString(ctx, name);

  JSValue result = JS_NewArray(ctx);
  if (JS_IsException(result)) {
    if (values) {
      for (int i = 0; i < count; i++) {
        free(values[i]);
      }
      free(values);
    }
    return result;
  }

  if (values) {
    for (int i = 0; i < count; i++) {
      JSValue value = JS_NewString(ctx, values[i]);
      if (JS_IsException(value)) {
        for (int j = i; j < count; j++) {
          free(values[j]);
        }
        free(values);
        JS_FreeValue(ctx, result);
        return value;
      }

      JS_SetPropertyUint32(ctx, result, i, value);
      free(values[i]);
    }
    free(values);
  }

  return result;
}

// URLSearchParams.prototype.has 方法
static JSValue js_url_search_params_has(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  URLSearchParams *params = JS_GetOpaque(this_val, js_url_search_params_class_id);
  if (!params)
    return JS_EXCEPTION;

  if (argc < 1)
    return JS_ThrowTypeError(ctx, "has requires at least 1 argument");

  const char *name = JS_ToCString(ctx, argv[0]);
  if (!name)
    return JS_EXCEPTION;

  bool has = url_search_params_has(params, name);

  JS_FreeCString(ctx, name);

  return JS_NewBool(ctx, has);
}

// URLSearchParams.prototype.set 方法
static JSValue js_url_search_params_set(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  URLSearchParams *params = JS_GetOpaque(this_val, js_url_search_params_class_id);
  if (!params)
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

  int ret = url_search_params_set(params, name, value);

  JS_FreeCString(ctx, name);
  JS_FreeCString(ctx, value);

  if (ret != 0) {
    return JS_ThrowOutOfMemory(ctx);
  }

  return JS_UNDEFINED;
}

// URLSearchParams.prototype.sort 方法
static JSValue js_url_search_params_sort(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  URLSearchParams *params = JS_GetOpaque(this_val, js_url_search_params_class_id);
  if (!params)
    return JS_EXCEPTION;

  url_search_params_sort(params);

  return JS_UNDEFINED;
}

// URLSearchParams.prototype.toString 方法
static JSValue js_url_search_params_to_string(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  URLSearchParams *usp = JS_GetOpaque(this_val, js_url_search_params_class_id);
  if (!usp)
    return JS_EXCEPTION;

  char *str = url_search_params_to_string(usp);
  if (!str)
    return JS_ThrowOutOfMemory(ctx);

  JSValue ret = JS_NewString(ctx, str);
  free(str);

  return ret;
}

// URLSearchParams.prototype.forEach 方法
static JSValue js_url_search_params_foreach(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  URLSearchParams *params = JS_GetOpaque(this_val, js_url_search_params_class_id);
  if (!params)
    return JS_EXCEPTION;

  if (argc < 1)
    return JS_ThrowTypeError(ctx, "forEach requires at least 1 argument");

  JSValueConst callback = argv[0];
  JSValueConst thisArg = argc > 1 ? argv[1] : JS_UNDEFINED;

  ParamNode *current = params->paramList;

  while (current) {
    JSValue value = JS_NewString(ctx, current->value);
    if (JS_IsException(value))
      return value;

    JSValue name = JS_NewString(ctx, current->name);
    if (JS_IsException(name)) {
      JS_FreeValue(ctx, value);
      return name;
    }

    JSValue args[3];
    args[0] = value;
    args[1] = name;
    args[2] = JS_DupValue(ctx, this_val);

    JSValue ret = JS_Call(ctx, callback, thisArg, 3, args);

    JS_FreeValue(ctx, value);
    JS_FreeValue(ctx, name);
    JS_FreeValue(ctx, args[2]);

    if (JS_IsException(ret))
      return ret;
    JS_FreeValue(ctx, ret);

    current = current->next;
  }

  return JS_UNDEFINED;
}

static JSValue js_create_url_search_params_iterator(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic) {
  URLSearchParams *params = JS_GetOpaque(this_val, js_url_search_params_class_id);
  if (!params)
    return JS_EXCEPTION;

  JSIteratorKindEnum kind;
  URLSearchParamsIterator *it;
  JSValue enum_obj;

  kind = magic >> 2;

  enum_obj = JS_NewObjectClass(ctx, js_url_search_params_iterator_class_id);
  if (JS_IsException(enum_obj))
    goto fail;

  it = js_malloc(ctx, sizeof(*it));
  if (!it) {
    JS_FreeValue(ctx, enum_obj);
    goto fail;
  }

  it->obj = JS_DupValue(ctx, this_val);
  it->kind = kind;
  it->current = NULL;

  JS_SetOpaque(enum_obj, it);

  return enum_obj;

fail:
  return JS_EXCEPTION;
}

static JSClassDef js_url_class_def = {
    "URL",
    .finalizer = js_url_finalizer,
};

static JSClassDef js_url_search_params_class = {
    "URLSearchParams",
    .finalizer = js_url_search_params_finalizer,
};

static JSClassDef js_url_search_params_iterator_class = {
    "URLSearchParamsIterator",
    .finalizer = js_url_search_params_iterator_finalizer,
};

static JSCFunctionListEntry js_url_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("href", js_url_get_property, NULL, 0),
    JS_CGETSET_MAGIC_DEF("protocol", js_url_get_property, NULL, 1),
    JS_CGETSET_MAGIC_DEF("hostname", js_url_get_property, NULL, 2),
    JS_CGETSET_MAGIC_DEF("host", js_url_get_property, NULL, 3),
    JS_CGETSET_MAGIC_DEF("pathname", js_url_get_property, NULL, 4),
    JS_CGETSET_MAGIC_DEF("search", js_url_get_property, NULL, 5),
    JS_CGETSET_MAGIC_DEF("hash", js_url_get_property, NULL, 6),
    JS_CGETSET_MAGIC_DEF("port", js_url_get_property, NULL, 7),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "URL", JS_PROP_CONFIGURABLE),
};

static JSCFunctionListEntry js_url_search_params_proto_funcs[] = {
    JS_CFUNC_DEF("append", 2, js_url_search_params_append),
    JS_CFUNC_DEF("delete", 1, js_url_search_params_delete),
    JS_CFUNC_DEF("get", 1, js_url_search_params_get),
    JS_CFUNC_DEF("getAll", 1, js_url_search_params_get_all),
    JS_CFUNC_DEF("has", 1, js_url_search_params_has),
    JS_CFUNC_DEF("set", 2, js_url_search_params_set),
    JS_CFUNC_DEF("sort", 0, js_url_search_params_sort),
    JS_CFUNC_DEF("forEach", 1, js_url_search_params_foreach),
    JS_CFUNC_MAGIC_DEF("values", 0, js_create_url_search_params_iterator, (JS_ITERATOR_KIND_VALUE << 2) | 0),
    JS_CFUNC_MAGIC_DEF("keys", 0, js_create_url_search_params_iterator, (JS_ITERATOR_KIND_KEY << 2) | 0),
    JS_CFUNC_MAGIC_DEF("entries", 0, js_create_url_search_params_iterator, (JS_ITERATOR_KIND_KEY_AND_VALUE << 2) | 0),
    JS_CFUNC_DEF("toString", 0, js_url_search_params_to_string),
    JS_ALIAS_DEF("[Symbol.iterator]", "entries"),
};

static const JSCFunctionListEntry js_url_search_params_iterator_proto_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 0, js_url_search_params_iterator_next, 0),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_create_url_search_params_iterator_iterator),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "URLSearchParams Iterator", JS_PROP_CONFIGURABLE),
};

void js_init_url(JSContext *ctx) {
  JSValue url_proto, url_class;
  JSValue url_search_params_proto, url_search_params_class;

  // ******************* URL *******************
  JS_NewClassID(&js_url_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_url_class_id, &js_url_class_def);

  url_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, url_proto, js_url_proto_funcs, countof(js_url_proto_funcs));

  url_class = JS_NewCFunction2(ctx, js_url_constructor, "URL", 1, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, url_class, url_proto);
  JS_SetClassProto(ctx, js_url_class_id, url_proto);

  // ******************* URLSearchParams *******************
  // Initialize the class ID
  JS_NewClassID(&js_url_search_params_class_id);
  // 初始化 URLSearchParams 类
  JS_NewClass(JS_GetRuntime(ctx), js_url_search_params_class_id, &js_url_search_params_class);

  // 初始化 Iterator 类
  JS_NewClassID(&js_url_search_params_iterator_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_url_search_params_iterator_class_id, &js_url_search_params_iterator_class);

  url_search_params_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, url_search_params_proto, js_url_search_params_proto_funcs, countof(js_url_search_params_proto_funcs));

  url_search_params_class = JS_NewCFunction2(ctx, js_url_search_params_constructor, "URLSearchParams", 1, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, url_search_params_class, url_search_params_proto);
  JS_SetClassProto(ctx, js_url_search_params_class_id, url_search_params_proto);

  // 创建 URLSearchParams Iterator 原型
  JSValue iterator_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, iterator_proto, js_url_search_params_iterator_proto_funcs, countof(js_url_search_params_iterator_proto_funcs));
  JS_SetClassProto(ctx, js_url_search_params_iterator_class_id, iterator_proto);

  JSValue global_obj = JS_GetGlobalObject(ctx);

  // Set the classes as global properties
  JS_SetPropertyStr(ctx, global_obj, "URL", url_class);
  JS_SetPropertyStr(ctx, global_obj, "URLSearchParams", url_search_params_class);

  JS_FreeValue(ctx, global_obj);
}
