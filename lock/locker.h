#ifndef LOCKER_H
#define LOCKER_H

#include <pthread.h>
#include <semaphore.h>

#include <exception>

class sem
{
   public:
	sem()  // 调用 sem_init 初始化信号量，初始值为 0。
	{
		if (sem_init(&m_sem, 0, 0) != 0)
		{  // 如果初始化失败，抛出
		   // std::exception()，表示构造失败（通常是权限问题或内存不足等）。
			throw std::exception();
		}
	}

	sem(int num)  // 调用 sem_init 初始化信号量，初始值为num。
	{
		if (sem_init(&m_sem, 0, num) != 0)
		{  // 第2个参数 0：表示这是线程间共享（非进程间共享）。
			throw std::exception();
		}
	}

	~sem()	// 在对象销毁时自动释放底层信号量资源，防止内存泄漏或资源泄露。
	{
		sem_destroy(&m_sem);
	}
	// RAII（资源获取即初始化）思想的体现。

	bool wait()
	{
		return sem_wait(&m_sem) == 0;
	}
	/*
		尝试获取一个资源：

		如果 m_sem > 0，则立即成功，并将 m_sem - 1。

		如果 m_sem == 0，则阻塞等待。
	*/

	bool post()	 // 释放一个资源
	{			 // 如果有线程因为信号量为0而在 sem_wait() 中阻塞，那么唤醒一个它。
		return sem_post(&m_sem) == 0;
	}

   private:
	sem_t m_sem;  // sem_t 是 POSIX 信号量（semaphore） 的数据类型
};

class locker
{
   public:
	locker()
	{
		if (pthread_mutex_init(&m_mutex, NULL) != 0)
		{
			throw std::exception();
		}
	}
	/*
		调用 pthread_mutex_init 初始化互斥锁。

		第二个参数为 NULL，表示使用默认属性。

		如果初始化失败，就抛出 std::exception()，防止程序继续运行。
	*/

	~locker()
	{
		pthread_mutex_destroy(&m_mutex);
	}
	/*
		销毁对象时自动调用。

		释放互斥锁资源，防止资源泄露。
	*/

	bool lock()
	{
		return pthread_mutex_lock(&m_mutex) == 0;
	}
	/*
		加锁（进入临界区）。

		如果锁当前被其他线程占用，当前线程会阻塞等待直到获得锁。

		返回值为 true 表示加锁成功。
	*/

	bool unlock()
	{
		return pthread_mutex_unlock(&m_mutex) == 0;
	}
	/*
		解锁（退出临界区）。

		解锁失败的可能情况：当前线程没有持有这把锁。

		返回值为 true 表示解锁成功。
	*/

	pthread_mutex_t *get()
	{
		return &m_mutex;
	}
	/*
		返回互斥锁对象的地址，便于传给其他需要 pthread_mutex_t* 参数的接口。

		举例：某些条件变量初始化时需要传入互斥锁地址。
	*/

   private:
	pthread_mutex_t m_mutex;
};
/*
	POSIX（Portable Operating System Interface）：是 UNIX
   类操作系统的标准接口规范。

	pthread（POSIX thread）：是 POSIX 定义的一套线程编程 API，广泛用于
   Linux、Unix 等系统。

	mutex（mutual exclusion，互斥）
*/

class cond
{
   public:
	cond()
	{
		if (pthread_cond_init(&m_cond, NULL) != 0)
		{
			throw std::exception();
		}

		// pthread_cond_init 初始化一个条件变量 m_cond。
		// 第二个参数传 NULL，表示使用默认属性。
		// 如果初始化失败，抛出异常。
	}

	~cond()
	{
		pthread_cond_destroy(&m_cond);

		// 析构时自动销毁条件变量，避免内存泄漏。
		// RAII 思路：对象生命周期结束时自动释放资源。
	}

	bool wait(pthread_mutex_t *m_mutex)
	{
		int ret = 0;

		ret = pthread_cond_wait(&m_cond, m_mutex);

		return ret == 0;
	}
	// pthread_cond_wait 会：
	// 自动解锁传入的 m_mutex；
	// 线程阻塞等待 m_cond 被唤醒；
	// 被唤醒后重新加锁 m_mutex，然后返回。
	// 线程在某个条件未满足时等待，直到其他线程发信号唤醒它。
	// 返回 true 表示成功，false 表示失败。

	bool timewait(pthread_mutex_t *m_mutex, struct timespec t)
	{
		int ret = 0;

		ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);

		return ret == 0;
	}
	// 和 wait 类似，但它支持超时等待。
	// 如果在指定的绝对时间点 t 之前收到信号，就返回 true；
	// 如果超时（ETIMEDOUT），返回 false。

	bool signal()
	{
		return pthread_cond_signal(&m_cond) == 0;
	}
	// 唤醒 至少一个 等待该条件变量的线程。
	// 常用于 "条件已满足，唤醒一个工作线程继续处理"。

	bool broadcast()
	{
		return pthread_cond_broadcast(&m_cond) == 0;
	}
	// 唤醒 所有 等待该条件变量的线程。
	// 常用于 "条件状态变化，所有等待的线程都应该重新检查条件"。

   private:
	pthread_cond_t m_cond;	// pthread_cond_t（POSIX 线程库里的条件变量）
};
// 这个 cond 类是一个轻量级的封装，核心功能就是：
// 构造 / 析构自动管理 pthread_cond_t 的生命周期；
// 提供 wait、timewait、signal、broadcast 四个常用接口。

#endif
