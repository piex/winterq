#ifndef WINTERQ_URL_H
#define WINTERQ_URL_H

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "quickjs.h"

#define URL_ERROR_INVALID_PROTOCOL 1
#define URL_ERROR_INVALID_URL 2
#define URL_ERROR_MEMORY 3

// URL 结构体
typedef struct {
  char *href;     // 完整的 URL 字符串
  char *protocol; // 协议（如 "http:", "https:"）
  char *hostname; // 主机名
  char *host;     // 主机名 + 端口
  char *pathname; // 路径
  char *search;   // 查询字符串（包括 "?"）
  char *hash;     // 哈希部分（包括 "#"）
  char *username; // 用户名
  char *password; // 密码
  int port;       // 端口号
} URL;

// 定义单个查询参数结构
typedef struct ParamNode {
  char *name;
  char *value;
  struct ParamNode *next;
} ParamNode;

// URLSearchParams 结构体
typedef struct {
  ParamNode *paramList;
} URLSearchParams;

// 初始化 URL 和 URLSearchParams 类
void js_init_url(JSContext *ctx);

bool url_is_valid_protocol(const char *protocol);
bool url_is_valid_hostname(const char *hostname);
URLSearchParams *url_search_params_new();

#endif // WINTERQ_URL_H
