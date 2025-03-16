#ifndef WINTERQ_LOG_H
#define WINTERQ_LOG_H

#include <stdio.h>
#include <time.h>

// 日志级别定义
#define WINTERQ_LOG_LEVEL_ERROR 1
#define WINTERQ_LOG_LEVEL_WARNING 2
#define WINTERQ_LOG_LEVEL_INFO 3
#define WINTERQ_LOG_LEVEL_DEBUG 4

// 设置当前日志级别（可在编译时通过 -DWINTERQ_LOG_LEVEL=2 进行调整）
#ifndef WINTERQ_LOG_LEVEL
#define WINTERQ_LOG_LEVEL WINTERQ_LOG_LEVEL_WARNING
#endif

// 获取当前时间字符串
static inline const char *winterq_log_timestamp()
{
  static char buffer[20];
  time_t t = time(NULL);
  struct tm *tm_info = localtime(&t);
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm_info);
  return buffer;
}

// 日志格式化宏
// clang-format off
#define WINTERQ_LOG_ERROR(format, ...)   do { if (WINTERQ_LOG_LEVEL >= WINTERQ_LOG_LEVEL_ERROR)   fprintf(stderr, "[%s] [ERROR]   %s:%d: " format "\n", winterq_log_timestamp(), __FILE__, __LINE__, ##__VA_ARGS__); } while(0)
#define WINTERQ_LOG_WARNING(format, ...) do { if (WINTERQ_LOG_LEVEL >= WINTERQ_LOG_LEVEL_WARNING) fprintf(stderr, "[%s] [WARNING] %s:%d: " format "\n", winterq_log_timestamp(), __FILE__, __LINE__, ##__VA_ARGS__); } while(0)
#define WINTERQ_LOG_INFO(format, ...)    do { if (WINTERQ_LOG_LEVEL >= WINTERQ_LOG_LEVEL_INFO)    fprintf(stdout, "[%s] [INFO]    %s:%d: " format "\n", winterq_log_timestamp(), __FILE__, __LINE__, ##__VA_ARGS__); } while(0)
#define WINTERQ_LOG_DEBUG(format, ...)    do { if (WINTERQ_LOG_LEVEL >= WINTERQ_LOG_LEVEL_DEBUG)  fprintf(stdout, "[%s] [DEBUG]   %s:%d: " format "\n", winterq_log_timestamp(), __FILE__, __LINE__, ##__VA_ARGS__); } while(0)
// clang-format on

#endif // WINTERQ_LOG_H
