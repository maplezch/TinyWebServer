#include "sql_connection_pool.h"

#include <mysql/mysql.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <iostream>
#include <list>
#include <string>

using namespace std;

connection_pool::connection_pool()	// 构造函数
{
	m_CurConn = 0;	 // 当前连接数量
	m_FreeConn = 0;	 // 当前空闲的连接数量
}

connection_pool *
connection_pool::GetInstance()	// 构造唯一一个connection_pool实例（单例模式）（懒汉模式）
{
	static connection_pool connPool;
	return &connPool;
}

// 构造初始化
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port,
						   int MaxConn, int close_log)
{
	m_url = url;	// 主机地址
	m_Port = Port;	// 数据库端口号
	m_User = User;	// 登陆数据库用户名
	m_PassWord = PassWord;
	m_DatabaseName = DBName;  // 使用数据库名
	m_close_log = close_log;

	for (int i = 0; i < MaxConn; i++)
	{
		MYSQL *con = NULL;	// MySQL C API 里定义的一个结构体指针，表示一个数据库连接句柄。
							// 里面存储了连接所需的各种状态，比如 socket、用户名、密码、数据库名等等

		con = mysql_init(con);	// 初始化一个 MYSQL 对象，为后续连接数据库做准备。

		if (con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(),
								 Port, NULL, 0);  // 建立数据库连接

		if (con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		connList.push_back(con);
		++m_FreeConn;
	}

	reserve = sem(m_FreeConn);

	m_MaxConn = m_FreeConn;
}

// 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
	MYSQL *con = NULL;

	if (0 == connList.size()) return NULL;	// 链接池中没有空闲

	reserve.wait();	 // 将信号量减一

	lock.lock();  // 互斥锁

	con = connList.front();	 // 获取一个连接
	connList.pop_front();

	--m_FreeConn;  // 可用的连接数减一
	++m_CurConn;   // 当前的总连接数加一

	lock.unlock();	// 互斥锁释放
	return con;		// 返回得到的连接
}

// 释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con)
{
	if (NULL == con) return false;

	lock.lock();

	connList.push_back(con);
	++m_FreeConn;
	--m_CurConn;

	lock.unlock();

	reserve.post();
	return true;
}

// 销毁数据库连接池
void connection_pool::DestroyPool()
{
	lock.lock();
	if (connList.size() > 0)
	{
		list<MYSQL *>::iterator it;
		for (it = connList.begin(); it != connList.end(); ++it)
		{
			MYSQL *con = *it;
			mysql_close(con);
		}
		m_CurConn = 0;
		m_FreeConn = 0;
		connList.clear();
	}

	lock.unlock();
}

// 当前空闲的连接数
int connection_pool::GetFreeConn()
{
	return this->m_FreeConn;
}

connection_pool::~connection_pool()
{
	DestroyPool();
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool)
{
	*SQL = connPool->GetConnection();

	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII()
{
	poolRAII->ReleaseConnection(conRAII);
}