#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

class sem
{
public:
    sem()   //调用 sem_init 初始化信号量，初始值为 0。
    {
        if (sem_init(&m_sem, 0, 0) != 0)
        {   //如果初始化失败，抛出 std::exception()，表示构造失败（通常是权限问题或内存不足等）。
            throw std::exception();
        }
    }

    sem(int num)    //调用 sem_init 初始化信号量，初始值为num。
    {
        if (sem_init(&m_sem, 0, num) != 0)
        {   //第2个参数 0：表示这是线程间共享（非进程间共享）。
            throw std::exception();
        }
    }

    ~sem()  //在对象销毁时自动释放底层信号量资源，防止内存泄漏或资源泄露。
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

    bool post()     //释放一个资源
    {   //如果有线程因为信号量为0而在 sem_wait() 中阻塞，那么唤醒一个它。
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
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
    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
    }
    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
    pthread_mutex_t *get()
    {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};


class cond
{
public:
    cond()
    {
        if (pthread_cond_init(&m_cond, NULL) != 0)
        {
            //pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }
    ~cond()
    {
        pthread_cond_destroy(&m_cond);
    }
    bool wait(pthread_mutex_t *m_mutex)
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_wait(&m_cond, m_mutex);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t)
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;
    }
    bool broadcast()
    {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    //static pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};
#endif
