#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <arpa/inet.h>	//提供 IP 地址相关的函数，常用于将 IP 地址（如字符串）转为网络字节顺序的二进制格式和反之。
#include <assert.h>
#include <errno.h>
#include <fcntl.h>	//提供文件控制操作函数，如 open、fcntl、O_CREAT 等，主要用于文件的打开、修改和操作文件描述符。
#include <netinet/in.h>	 //提供 Internet 协议族（IPV4）的相关定义，包含一些常用的网络结构和宏（如 sockaddr_in、AF_INET、INADDR_ANY）。
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>	 //提供处理可变参数函数的支持，如 va_start、va_arg、va_end 等。
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>  //提供内存映射操作相关的功能，如 mmap、munmap 等，用于内存管理，通常用于文件映射和共享内存。
#include <sys/socket.h>
#include <sys/stat.h>  //提供文件状态信息的函数，如 stat、fstat、chmod 等，用于获取文件的属性（如大小、权限、时间戳等）
#include <sys/types.h>	//定义了一些数据类型，如 pid_t、size_t 等，它们常常用于系统调用。
#include <sys/uio.h>  //提供与 I/O 向量（iovec）相关的功能，常用于高效的 I/O 操作，尤其是在处理多缓冲区 I/O 时。
#include <sys/wait.h>
#include <unistd.h>	 //提供对操作系统功能的访问，包含系统调用（如 fork、exec、sleep 等）以及文件操作（如 read、write、close）等。

#include <map>

#include "../CGImysql/sql_connection_pool.h"
#include "../lock/locker.h"
#include "../log/log.h"
#include "../timer/lst_timer.h"

class http_conn
{
   public:
	static const int FILENAME_LEN = 200;		// 文件名长度
	static const int READ_BUFFER_SIZE = 2048;	// 读取缓冲区
	static const int WRITE_BUFFER_SIZE = 1024;	// 写入缓冲区

	enum METHOD	 // 枚举定义HTTP请求的方法
	{
		GET = 0,
		POST,
		HEAD,
		PUT,
		DELETE,
		TRACE,
		OPTIONS,
		CONNECT,
		PATH
	};

	enum CHECK_STATE  // 枚举定义了请求处理的三个阶段：请求行的检查、头部的检查和内容的检查。
	{
		CHECK_STATE_REQUESTLINE = 0,
		CHECK_STATE_HEADER,
		CHECK_STATE_CONTENT
	};

	enum HTTP_CODE	// 枚举定义了 HTTP 请求的处理结果代码
	{
		NO_REQUEST,
		GET_REQUEST,
		BAD_REQUEST,
		NO_RESOURCE,
		FORBIDDEN_REQUEST,
		FILE_REQUEST,
		INTERNAL_ERROR,
		CLOSED_CONNECTION
	};

	enum LINE_STATUS  // 枚举定义了 HTTP 请求解析过程中行的状态（如有效行、无效行、行未完成）。
	{
		LINE_OK = 0,
		LINE_BAD,
		LINE_OPEN
	};

   public:
	http_conn() {}
	~http_conn() {}

   public:
	void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd,
			  string sqlname);

	void close_conn(bool real_close = true);  // 关闭连接

	void process();

	bool read_once();

	bool write();

	sockaddr_in *get_address()
	{
		return &m_address;
	}

	void initmysql_result(connection_pool *connPool);

	int timer_flag;

	int improv;

   private:
	void init();

	HTTP_CODE process_read();

	bool process_write(HTTP_CODE ret);

	HTTP_CODE parse_request_line(char *text);
	HTTP_CODE parse_headers(char *text);
	HTTP_CODE parse_content(char *text);

	HTTP_CODE do_request();

	char *get_line()
	{
		return m_read_buf + m_start_line;
	};
	LINE_STATUS parse_line();

	void unmap();

	bool add_response(const char *format, ...);

	bool add_content(const char *content);

	bool add_status_line(int status, const char *title);

	bool add_headers(int content_length);

	bool add_content_type();

	bool add_content_length(int content_length);

	bool add_linger();

	bool add_blank_line();

   public:
	static int m_epollfd;  // epoll 的文件描述符

	static int m_user_count;  // 当前连接的用户数量

	MYSQL *mysql;

	int m_state;  // 读为0, 写为1

   private:
	int m_sockfd;  // 与客户端连接的套接字描述符

	sockaddr_in m_address;	// 客户端的地址信息

	char m_read_buf[READ_BUFFER_SIZE];	// 读取http数据的缓冲区

	long m_read_idx;	 // 读取位置
	long m_checked_idx;	 // 检查位置
	int m_start_line;	 // 行的起始位置

	char m_write_buf[WRITE_BUFFER_SIZE];  // 写入http数据的缓冲区

	int m_write_idx;			// 写入位置
	CHECK_STATE m_check_state;	// 请求状态
	METHOD m_method;			// 请求方法（METHOD）

	char m_real_file[FILENAME_LEN];	 // 请求的文件的实际路径

	char *m_url;			// url
	char *m_version;		// 版本
	char *m_host;			// 主机
	long m_content_length;	// 内容长度

	bool m_linger;	// 是否保持连接

	char *m_file_address;  // 文件的地址

	struct stat m_file_stat;  // 文件状态信息

	struct iovec m_iv[2];  // 存储写入操作的数据块，用于支持 writev 系统调用。
	// iovec 是一个标准的结构体类型，定义在 <sys/uio.h> 头文件中，用于支持 writev 系统调用。

	// writev() 是一个高效的 I/O 系统调用，它可以一次性将多个缓冲区的数据写入套接字，避免了多次调用
	// write() 系统调用。每个 iovec 结构体表示一个独立的缓冲区，可以是不同的内存区域。

	int m_iv_count;	 // 记录 m_iv 数组中有效元素的数量。用来告知 writev()
					 // 系统调用实际需要写入的数据块数量。

	int cgi;  // 是否启用的POST
	// CGI 是一种标准，允许 Web 服务器与外部应用程序（如脚本）进行交互。当服务器接收到一个 POST
	// 请求时，CGI 会根据请求的内容，调用相应的脚本（如 PHP、Python、Perl 等）来处理请求并生成响应。

	char *m_string;	 // 存储请求头数据

	int bytes_to_send;	// 要发送的数据大小

	int bytes_have_send;  // 已发送的数据大小

	char *doc_root;	 // web服务器的根目录

	map<string, string> m_users;  // 存储用户信息的映射表

	int m_TRIGMode;	 // 触发模式

	int m_close_log;  // 是否关闭日志

	// 数据库相关
	char sql_user[100];
	char sql_passwd[100];
	char sql_name[100];
};

#endif
