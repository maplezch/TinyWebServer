#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>

#include <cstdio>
#include <exception>
#include <list>

#include "../CGImysql/sql_connection_pool.h"
#include "../lock/locker.h"

template <typename T>
class threadpool
{
   public:
	/*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
	threadpool(int actor_model, connection_pool *connPool, int thread_number = 8,
			   int max_request = 10000);

	~threadpool();

	bool append(T *request, int state);

	bool append_p(T *request);

   private:
	/*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
	static void *worker(void *arg);

	void run();

   private:
	int m_thread_number;  // 线程池中的线程数

	int m_max_requests;	 // 请求队列中允许的最大请求数

	pthread_t *m_threads;  // 描述线程池的数组，其大小为m_thread_number

	std::list<T *> m_workqueue;	 // 请求队列

	locker m_queuelocker;  // 保护请求队列的互斥锁

	sem m_queuestat;  // 是否有任务需要处理

	connection_pool *m_connPool;  // 数据库

	int m_actor_model;	// 模型切换
};

template <typename T>
threadpool<T>::threadpool(int actor_model, connection_pool *connPool, int thread_number,
						  int max_requests)
	: m_actor_model(actor_model),	   // 初始化模型
	  m_thread_number(thread_number),  // 初始化线程数
	  m_max_requests(max_requests),	   // 初始化请求队列最大数量
	  m_threads(NULL),				   // 线程数组
	  m_connPool(connPool)			   // 数据库连接池初始化
{
	if (thread_number <= 0 || max_requests <= 0)
		throw std::exception();	 // 线程数量和最大队列请求数量设置错误

	m_threads = new pthread_t[m_thread_number];	 // 线程数组初始化

	if (!m_threads) throw std::exception();	 // 空间初始化失败

	for (int i = 0; i < thread_number; ++i)	 // 创建线程
	{
		if (pthread_create(m_threads + i, NULL, worker, this) != 0)	 // 非0为创建失败
		{
			// 参数 1 (m_threads + i): 这是一个指向 pthread_t 类型数组（可能是 m_threads）中第 i
			// 个元素的指针。新创建线程的 ID 将被存储在这个位置。
			// 参数 2(NULL): 用于指定线程属性（如栈大小、调度策略等）。NULL 表示使用默认属性。
			// 参数 3(worker): 这是一个函数指针，指定新线程开始执行的函数。所有新线程都会从执行这个
			// worker函数开始。
			//   参数 4(this): 这是传递给 worker 函数的参数。在C++ 类中，传递 this
			//   指针通常是为了让线程函数能够访问并操作当前对象的成员变量和方法。

			delete[] m_threads;	 // 清理之前分配的线程

			throw std::exception();
		}

		if (pthread_detach(m_threads[i]))  // 让线程以分离（detached）状态运行，0为成功，非0出错
		{
			// 默认情况下，pthread 创建的线程是“可连接（joinable）”的。
			// 主线程需要调用 pthread_join() 来等待其结束并释放资源；
			// 否则线程结束后会变成“僵尸线程”，占用系统资源。
			// pthread_detach() 的作用是：告诉系统：线程结束后，自动释放所有资源，不需要
			// pthread_join()。

			delete[] m_threads;	 // 释放之前分配的线程数组，避免内存泄露

			throw std::exception();
		}
	}
}

template <typename T>
threadpool<T>::~threadpool()  // 析构函数
{
	delete[] m_threads;	 // 释放资源
}

template <typename T>
bool threadpool<T>::append(T *request, int state)  // 将新任务加入队列
{
	m_queuelocker.lock();  // 将请求队列用互斥锁锁住

	if (m_workqueue.size() >= m_max_requests)  // 队列的大小大于等于最大等待的数量，无法加入
	{
		m_queuelocker.unlock();	 // 解锁

		return false;
	}

	request->m_state = state;  // 设置任务的状态。
							   // 在 HTTP 服务器中，这个状态通常表示：
							   // state = 0 → 读请求；
							   // state = 1 → 写请求；

	m_workqueue.push_back(request);	 // 加入等待队列

	m_queuelocker.unlock();	 // 解锁

	m_queuestat.post();	 // 通知信号量（m_queuestat）增加一个可用任务。
						 // 这一步的作用是唤醒阻塞等待任务的工作线程。
						 // 工作线程通常在 wait() 信号量时休眠；
	// 当有任务时，post() 会让其中一个线程醒来处理任务。

	return true;
}

template <typename T>
bool threadpool<T>::append_p(T *request)  // 不区分状态的添加任务
{
	m_queuelocker.lock();

	if (m_workqueue.size() >= m_max_requests)
	{
		m_queuelocker.unlock();
		return false;
	}

	m_workqueue.push_back(request);

	m_queuelocker.unlock();

	m_queuestat.post();

	return true;
}

template <typename T>
void *threadpool<T>::worker(void *arg)
{
	threadpool *pool = (threadpool *) arg;	// 把 void* 转回原类型：create函数要求第四个参数为void*

	pool->run();

	return pool;
}

template <typename T>
void threadpool<T>::run()
{
	while (true)
	{
		m_queuestat.wait();
		// 如果任务队列为空，线程会阻塞在这里等待。
		// 当有新任务加入时，append() 会调用 m_queuestat.post() 唤醒线程。

		m_queuelocker.lock();  // 队列加锁

		if (m_workqueue.empty())  // 任务队列为空
		{
			m_queuelocker.unlock();	 // 解锁

			continue;
		}

		T *request = m_workqueue.front();  // 取出队首任务

		m_workqueue.pop_front();  // 弹出队首任务

		m_queuelocker.unlock();	 // 解锁队列

		if (!request) continue;	 // 队列为空则继续下一个循环

		if (1 == m_actor_model)	 // Reactor 模型（I/O 事件驱动，先读写再处理）
		{
			if (0 == request->m_state)	// 0 → 读事件
			{
				if (request->read_once())  // 尝试一次性读取请求数据
				{						   // 一次性读取成功
					request->improv =
						1;	//// 改进标志位（常用于同步或线程池通信时标记该连接的处理进展）

					connectionRAII mysqlcon(&request->mysql,
											m_connPool);  // RAII 管理 MySQL 连接，确保自动释放

					request->process();	 // 任务处理
				}
				else
				{
					request->improv = 1;

					request->timer_flag = 1;  // 标记超时定时器需要处理
				}
			}
			else  // 写事件
			{
				if (request->write())  // 尝试写
				{
					request->improv = 1;
				}
				else  // 写失败
				{
					request->improv = 1;

					request->timer_flag = 1;
				}
			}
		}
		else  // Proactor 模型（I/O 完成直接处理）
		{
			connectionRAII mysqlcon(&request->mysql, m_connPool);

			request->process();
		}

		// Reactor：事件驱动 → 应用程序执行 I/O → 再处理
		// Proactor：I / O 完成驱动 → 应用程序处理结果
	}
}

#endif
