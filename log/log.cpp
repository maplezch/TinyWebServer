#include "log.h"

#include <pthread.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

using namespace std;

Log::Log()
{
	m_count = 0;		 // 当前日志行数
	m_is_async = false;	 // 是否同步标志位（初始为同步）
}

Log::~Log()
{
	if (m_fp != NULL)  // 关闭日志文件
	{
		fclose(m_fp);
	}
}

// 异步需要设置阻塞队列的长度，同步不需要设置
bool Log::init(const char *file_name, int close_log, int log_buf_size, int split_lines,
			   int max_queue_size)
{
	// 如果设置了max_queue_size,则设置为异步
	if (max_queue_size >= 1)
	{
		m_is_async = true;										// 设置为异步
		m_log_queue = new block_queue<string>(max_queue_size);	// 阻塞队列
		pthread_t tid;											// 线程id
		// flush_log_thread为回调函数,这里表示创建线程异步写日志
		pthread_create(&tid, NULL, flush_log_thread, NULL);
		// &tid：指向 pthread_t 的指针，创建成功后，新线程的 ID 会写入到 tid 中。
		// NULL：线程的属性，这里传 NULL 表示使用默认属性（例如默认堆栈大小、默认调度策略等）。
		// flush_log_thread：新线程的入口函数指针，线程启动后会从这个函数开始运行。
		// NULL：传递给 flush_log_thread 的参数。
	}

	m_close_log = close_log;  // 是否关闭日志
	m_log_buf_size = log_buf_size;
	m_buf = new char[m_log_buf_size];  // 缓冲区
	memset(m_buf, '\0', m_log_buf_size);
	m_split_lines = split_lines;

	time_t t = time(NULL);	// 获取当前的时间戳（从 1970 年 1 月 1 日到现在的秒数）
	struct tm *sys_tm =
		localtime(&t);			// 把时间戳转化为本地时间（年月日、时分秒等），返回一个 struct tm*。
	struct tm my_tm = *sys_tm;	// 把指针内容拷贝一份到 my_tm，避免 sys_tm 的内容被覆盖。

	const char *p =
		strrchr(file_name, '/');  // strrchr(file_name, '/')：在文件名字符串中，查找最后一个 /。
								  // 如果找到了，说明 file_name 包含目录，比如 "logs/app.log"。
	// 如果没找到（返回 NULL），说明 file_name 只是一个纯文件名，比如"app.log"。

	char log_full_name[256] = {0};	// 定义一个字符串缓冲区，用来存放拼接后的完整日志文件名。

	if (p == NULL)
	{
		snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1,
				 my_tm.tm_mday, file_name);	 // 设置log_full_name的内容
	}
	else
	{
		strcpy(log_name, p + 1);						  // 截取出文件名
		strncpy(dir_name, file_name, p - file_name + 1);  // 将路径部分拷贝至dir_name
		snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900,
				 my_tm.tm_mon + 1, my_tm.tm_mday, log_name);  // 设置log_full_name的内容
	}

	m_today = my_tm.tm_mday;

	m_fp = fopen(log_full_name, "a");  // 打开文件
	if (m_fp == NULL)				   // 检查错误
	{
		return false;
	}

	return true;
}

void Log::write_log(int level, const char *format, ...)
{
	struct timeval now = {0, 0};  // 表示 时间点（精确到微秒）

	gettimeofday(&now, NULL);  // 1970年到现在的当前时间

	time_t t = now.tv_sec;

	struct tm *sys_tm = localtime(&t);	// 转换为本地时间

	struct tm my_tm = *sys_tm;

	char s[16] = {0};
	switch (level)
	{
		case 0:
			strcpy(s, "[debug]:");
			break;
		case 1:
			strcpy(s, "[info]:");
			break;
		case 2:
			strcpy(s, "[warn]:");
			break;
		case 3:
			strcpy(s, "[erro]:");
			break;
		default:
			strcpy(s, "[info]:");
			break;
	}

	// 写入一个log，对m_count++, m_split_lines最大行数
	m_mutex.lock();
	m_count++;

	if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0)  // everyday log
	{
		// m_today != my_tm.tm_mday表示当前系统的“日”发生了变化（即跨天了），需要新建当天的日志文件
		// m_count % m_split_lines == 0表示日志条数达到了分割阈值
		// m_split_lines，需要切出一个新的日志文件

		fflush(m_fp);  // 刷新缓冲区，把日志缓冲中的数据写入文件。
		fclose(m_fp);  // 关闭当前正在写的日志文件

		char new_log[256] = {0};

		char tail[16] = {0};

		snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

		if (m_today !=
			my_tm.tm_mday)	// 当前系统的“日”发生了变化（即跨天了），需要新建当天的日志文件
		{
			snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
			m_today = my_tm.tm_mday;
			m_count = 0;
		}
		else
		{
			snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name,
					 m_count / m_split_lines);
		}
		m_fp = fopen(new_log, "a");
	}

	m_mutex.unlock();

	va_list valst;			  // 一个用于遍历可变参数的类型（在 <stdarg.h> / <cstdarg> 中定义）
	va_start(valst, format);  // 把 valst 初始化为指向 format 之后第一个可变参数的位置，使你可以用
							  // va_arg 逐个读取可变参数。

	string log_str;
	m_mutex.lock();

	// 写入的具体时间内容格式
	int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ", my_tm.tm_year + 1900,
					 my_tm.tm_mon + 1, my_tm.tm_mday, my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec,
					 now.tv_usec, s);

	int m = vsnprintf(m_buf + n, m_log_buf_size - n - 1, format, valst);
	// vsnprintf 会根据 format 中的格式说明符读取 valst 中的参数并格式化字符串。vsnprintf 会消耗
	// valst 的状态（即读取参数）。

	m_buf[n + m] = '\n';
	m_buf[n + m + 1] = '\0';
	log_str = m_buf;

	m_mutex.unlock();

	if (m_is_async &&
		!m_log_queue->full())  // 如果是异步日志模式（m_is_async == true），并且日志队列没满 →
							   // 把日志字符串 push 到队列里。后台线程会从队列里取出日志并写文件。
	{
		m_log_queue->push(log_str);
	}
	else  // 否则（同步模式或队列满了），直接写入文件 m_fp。
	{
		m_mutex.lock();
		fputs(log_str.c_str(), m_fp);  // 把内容放到m_fp的系统提供的缓冲区
		m_mutex.unlock();
	}

	va_end(valst);	// 清理 va_list，告诉编译器参数访问已经结束。
					// 释放/清理与 valst 相关的内部状态（对某些实现是必须的）
}

void Log::flush(void)
{
	m_mutex.lock();
	// 强制刷新写入流缓冲区
	fflush(m_fp);  // 强制把文件流 m_fp 的缓冲区（系统提供的，不是m_buff)内容立刻写到文件中。
	m_mutex.unlock();
}
