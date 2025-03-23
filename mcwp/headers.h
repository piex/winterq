#ifndef WINTERQ_HEADERS_H
#define WINTERQ_HEADERS_H

#include "quickjs.h"

// 定义 Headers 保护模式枚举
typedef enum { GUARD_NONE, GUARD_IMMUTABLE, GUARD_REQUEST, GUARD_REQUEST_NO_CORS, GUARD_RESPONSE } HeadersGuard;

// 定义单个 header 结构
typedef struct HeaderNode {
  char *name;
  char *value;
  struct HeaderNode *next;
} HeaderNode;

// 定义 Headers 对象
typedef struct {
  HeaderNode *headerList;
  HeadersGuard guard;
} Headers;

void js_init_headers(JSContext *ctx);

#endif /* WINTERQ_HEADERS_H */
