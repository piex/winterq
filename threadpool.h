#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>

#include "runtime.h"

/**
 * @brief 表示一个将要执行的JavaScript任务
 */
typedef struct Task
{
  int task_id; // 任务唯一 id

  // JavaScript源代码或字节码(二选一)
  bool is_script;      // true 表示是脚本字符串，false 表示是字节码
  const char *script;  // JavaScript源代码字符串
  uint8_t *bytecode;   // JavaScript字节码
  size_t bytecode_len; // 字节码长度

  // 性能指标
  clock_t start_time;    // 任务开始执行的时间
  double execution_time; // 任务执行时间(秒)

  // 回调机制
  void (*callback)(void *); // 任务完成后的回调函数
  void *callback_arg;       // 回调函数的参数

  struct ThreadPool *pool; // 指向线程池的指针
} Task;

/**
 * @brief 任务队列节点，用于链表实现的任务队列
 */
typedef struct TaskNode
{
  Task *task;
  struct TaskNode *next;
} TaskNode;

/**
 * @brief 线程安全的任务队列
 */
typedef struct TaskQueue
{
  TaskNode *head;  // 队列头
  TaskNode *tail;  // 队列尾
  int size;        // 队列中任务数量
  size_t max_size; // 队列最大容量，0表示无限制

  // 同步机制
  pthread_mutex_t mutex;    // 队列互斥锁
  pthread_cond_t not_empty; // 队列非空条件变量
  pthread_cond_t not_full;  // 队列未满条件变量
} TaskQueue;

// Thread pool configuration
typedef struct ThreadPoolConfig
{
  int thread_count; // 线程数量
  int max_contexts; // 每个运行时的最大上下文数

  size_t global_queue_size; // 全局队列大小，0表示无限
  size_t local_queue_size;  // 本地队列大小，0表示无限

  bool enable_work_stealing; // 是否启用工作窃取
  int idle_threshold;        // 空闲线程阈值，用于动态调整线程数
  bool dynamic_sizing;       // 是否动态调整线程池大小
} ThreadPoolConfig;

/**
 * @brief 每个工作线程的数据
 */
typedef struct ThreadData
{
  int thread_id;           // 线程ID
  struct ThreadPool *pool; // 所属线程池指针

  int max_contexts;       // 最大JS上下文数
  WorkerRuntime *runtime; // WorkerRuntime 指针
  TaskQueue local_queue;  // 线程本地任务队列

  // 性能统计
  atomic_bool idle;                // 线程是否空闲
  atomic_int tasks_processed;      // 该线程处理的任务数量
  uint64_t idle_start;             // 开始计时时间
  atomic_uint_least64_t idle_time; // 线程空闲的累计时间(毫秒)
  atomic_uint_least64_t busy_time; // 线程忙碌的累计时间(毫秒)
} ThreadData;

/**
 * @brief Thread pool statistics
 */
typedef struct ThreadPoolStats
{
  int active_threads;        // 当前活跃线程数
  int idle_threads;          // 当前空闲线程数
  int queued_tasks;          // 队列中等待的任务数
  int completed_tasks;       // 已完成的任务数
  double avg_wait_time;      // 平均等待时间(毫秒)
  double avg_execution_time; // 平均执行时间(毫秒)
  double thread_utilization; // 线程利用率(百分比)
} ThreadPoolStats;

/**
 * @brief 线程池主结构
 */
typedef struct ThreadPool
{
  pthread_t *threads;      // 线程数组
  ThreadData *thread_data; // 线程数据数组
  int thread_count;        // 线程数
  atomic_bool shutdown;    // 关闭标志

  // 任务统计
  int max_tasks;              // 最大任务数量
  atomic_int completed_tasks; // 已执行任务计数
  atomic_int total_tasks;     // 总任务计数

  // 同步机制
  pthread_mutex_t pool_mutex; // 线程池互斥锁
  pthread_mutex_t wait_mutex; // 等待互斥锁
  pthread_cond_t wait_cond;   // 等待条件变量

  TaskQueue queue; // 全局任务队列

  ThreadPoolConfig config; // 线程池配置

  // 用于管理空闲线程的数据结构
  atomic_int idle_thread_count; // 空闲线程计数
  pthread_mutex_t idle_mutex;   // 空闲线程互斥锁
  pthread_cond_t idle_cond;     // 空闲线程条件变量

  // 用于动态调整线程池大小
  pthread_t adjuster_thread;    // 调整线程
  atomic_bool adjuster_running; // 调整线程是否运行
} ThreadPool;

// API declarations

/**
 * @brief 初始化线程池
 * @param config 线程池配置
 * @return 成功返回线程池指针，失败返回NULL
 */
ThreadPool *init_thread_pool(ThreadPoolConfig config);

/**
 * @brief 添加JavaScript脚本任务到线程池
 * @param pool 线程池
 * @param script JavaScript脚本字符串
 * @param callback 任务完成后的回调函数
 * @param callback_arg 回调函数参数
 * @return 成功返回0，失败返回-1
 */
int add_script_task_to_pool(ThreadPool *pool, const char *script, void (*callback)(void *), void *callback_arg);

/**
 * @brief 添加字节码任务到线程池
 * @param pool 线程池
 * @param bytecode JavaScript字节码
 * @param bytecode_len 字节码长度
 * @param callback 回调函数
 * @param callback_arg 回调函数参数
 * @return 成功返回0，失败返回-1
 */
int add_bytecode_task_to_pool(ThreadPool *pool, uint8_t *bytecode, size_t bytecode_len, void (*callback)(void *), void *callback_arg);

/**
 * @brief 关闭线程池并释放资源
 * @param pool 线程池
 */
void shutdown_thread_pool(ThreadPool *pool);

/**
 * @brief 获取线程池统计信息
 * @param pool 线程池
 * @return 线程池统计信息
 */
ThreadPoolStats get_thread_pool_stats(ThreadPool *pool);

/**
 * @brief 等待线程池中所有任务完成
 * @param pool 线程池
 * @param timeout_ms 超时时间(毫秒)，0表示无限等待
 * @return 成功返回0，超时返回1，失败返回-1
 */
int wait_for_idle(ThreadPool *pool, int timeout_ms);

/**
 * @brief 动态调整线程池大小
 * @param pool 线程池
 * @param new_thread_count 新的线程数量
 * @return 成功返回0，失败返回-1
 */
int resize_thread_pool(ThreadPool *pool, int new_thread_count);

/**
 * @brief 获取指定线程的统计信息
 * @param pool 线程池
 * @param thread_id 线程ID
 * @param stats 用于存储统计信息的结构体指针
 * @return 成功返回0，失败返回-1
 */
int get_thread_stats(ThreadPool *pool, int thread_id, ThreadData *stats);

#endif // THREADPOOL_H
