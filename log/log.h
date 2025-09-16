#ifndef LOG_H
#define LOG_H

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>

#include <iostream>
#include <string>

#include "block_queue.h"

using namespace std;

class Log
{
   public:
	// C++11以后,使用局部变量懒汉不用加锁
	static Log *get_instance()	// 返回一个 指向唯一 Log 对象的指针。
	{
		static Log instance;  // 局部静态变量，只会构造一次
		return &instance;	  // 返回它的地址
	}

	static void *flush_log_thread(	// pthread_create的要求使用void *
		void *args)	 // 写日志的线程（启动一个新线程，让它专门跑日志系统的“异步写”逻辑）
	{
		Log::get_instance()->async_write_log();
	}

	// 可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
	bool init(const char *file_name, int close_log, int log_buf_size = 8192,
			  int split_lines = 5000000, int max_queue_size = 0);
	// close_log可能是控制是否写日志

	void write_log(int level, const char *format, ...);

	void flush(void);

   private:
	Log();

	virtual ~Log();

	void *async_write_log()
	{
		string single_log;
		// 从阻塞队列中取出一个日志string，写入文件
		while (m_log_queue->pop(single_log))
		{
			m_mutex.lock();

			fputs(single_log.c_str(), m_fp);
			// single_log 是一个 std::string 对象。
			// .c_str() 会返回指向其内部 以 \0 结尾的 C 风格字符串 的指针。
			// 这样就能把 std::string 传给只接受 const char *的 C 函数。

			m_mutex.unlock();
		}
	}

   private:
	char dir_name[128];	 // 路径名
	char log_name[128];	 // log文件名
	int m_split_lines;	 // 日志最大行数
	int m_log_buf_size;	 // 日志缓冲区大小
	long long m_count;	 // 日志行数记录
	int m_today;		 // 因为按天分类,记录当前时间是那一天
	FILE *m_fp;			 // 打开log的文件指针
	char *m_buf;
	block_queue<string> *m_log_queue;  // 阻塞队列
	bool m_is_async;				   // 是否同步标志位
	locker m_mutex;
	int m_close_log;  // 关闭日志
};

#define LOG_DEBUG(format, ...)                                    \
	if (0 == m_close_log)                                         \
	{                                                             \
		Log::get_instance()->write_log(0, format, ##__VA_ARGS__); \
		Log::get_instance()->flush();                             \
	}
#define LOG_INFO(format, ...)                                     \
	if (0 == m_close_log)                                         \
	{                                                             \
		Log::get_instance()->write_log(1, format, ##__VA_ARGS__); \
		Log::get_instance()->flush();                             \
	}
#define LOG_WARN(format, ...)                                     \
	if (0 == m_close_log)                                         \
	{                                                             \
		Log::get_instance()->write_log(2, format, ##__VA_ARGS__); \
		Log::get_instance()->flush();                             \
	}
#define LOG_ERROR(format, ...)                                    \
	if (0 == m_close_log)                                         \
	{                                                             \
		Log::get_instance()->write_log(3, format, ##__VA_ARGS__); \
		Log::get_instance()->flush();                             \
	}

#endif
