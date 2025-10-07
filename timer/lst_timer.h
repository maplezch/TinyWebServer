#ifndef LST_TIMER
#define LST_TIMER

#include <arpa/inet.h>	//提供了与 Internet 地址转换相关的函数，例如将 IP 地址从字符串形式转换为网络字节序的整数形式，或反向转换。
#include <assert.h>
#include <errno.h>
#include <fcntl.h>	//提供了文件控制操作的接口，例如文件描述符操作，文件打开标志等。
#include <netinet/in.h>	 //包含网络地址相关的结构和常量，特别是与 sockaddr_in 结构体和 Internet 协议族（IPv4）相关的定义。
#include <pthread.h>
#include <signal.h>	 //提供了对信号处理的支持，允许程序处理操作系统发出的信号，如中断、停止等。
#include <stdarg.h>	 //提供了处理可变参数函数的支持
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>	//提供了对 Linux 的 epoll I/O 多路复用机制的支持。epoll 是一种高效的事件驱动模型，用于处理大量并发 I/O 操作，常用于网络服务器中。
#include <sys/mman.h>	//提供了对内存映射的支持，允许进程将文件或设备直接映射到内存空间。
#include <sys/socket.h>
#include <sys/stat.h>	//提供了对文件和目录的属性的访问，支持获取文件状态、权限等操作。
#include <sys/types.h>	//定义了很多数据类型，主要是与系统调用相关的类型
#include <sys/uio.h>  //提供了与 I/O 操作相关的功能，尤其是对 readv() 和 writev() 的支持，允许一次性对多个缓冲区进行读写操作。这通常用于提高 I/O 性能。
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>	 //提供了对 POSIX 操作系统 API 的访问，包含与进程管理、文件操作、环境变量、内存管理等相关的函数

#include "../log/log.h"

class util_timer;

struct client_data
{
	sockaddr_in address;  // 存储客户端的地址信息
	// sockaddr_in 是一个 struct，它是 sockaddr 结构体的一个变种，专门用于存储 IPv4 地址。

	// sockaddr_in 结构通常包含以下内容：
	// sin_family：地址族，通常为 AF_INET，表示 IPv4 地址。
	// sin_port：客户端的端口号（通常使用
	// htons() 函数将主机字节序转换为网络字节序）。
	// sin_addr：客户端的 IP 地址（通常是一个 in_addr 结构体，存储 IPv4 地址）。

	int sockfd;	 // 套接字描述符，表示与客户端建立的通信通道。套接字描述符是操作系统用于标识一个网络连接的整数值。

	util_timer *timer;	// 指向定时器对象的指针
};

class util_timer  // 定时器类
{
   public:
	util_timer() : prev(NULL), next(NULL) {}  // 构造函数，将 prev 和 next 成员变量初始化为 NULL

   public:
	time_t expire;	// 过期时间

	void (*cb_func)(client_data *);	 // cb_func 是一个指向函数的指针，这个函数接受一个 client_data*
									 // 类型的参数，并返回 void

	client_data *user_data;

	util_timer *prev;
	util_timer *next;
};

class sort_timer_lst  // 实现一个定时器列表，这个列表是按定时器过期时间升序排序的。
{
   public:
	sort_timer_lst();
	~sort_timer_lst();

	void add_timer(util_timer *timer);	// 添加一个定时器到列表

	void adjust_timer(util_timer *timer);  // 调整定时器在列表中的位置

	void del_timer(util_timer *timer);	// 删除指定的定时器

	void
	tick();	 // 用于定时器轮询，它会检查定时器列表中所有定时器是否已过期，并且如果有定时器过期了，就执行相应的回调函数或者删除过期的定时器。

   private:
	void add_timer(util_timer *timer, util_timer *lst_head);

	util_timer *head;  // 定时器列表的头
	util_timer *tail;  // 定时器列表的尾
};

class Utils
{
   public:
	Utils() {}
	~Utils() {}

	void init(int timeslot);

	// 对文件描述符设置非阻塞
	int setnonblocking(int fd);
	// 非阻塞模式意味着如果当前没有数据可读，或者当前不能执行某个操作，系统调用不会被阻塞，函数会立刻返回一个错误（例如
	// EAGAIN）。这在高并发网络编程中很有用，避免了程序在等待某个操作时被阻塞。

	// 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
	void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);
	// 用于将文件描述符 fd 添加到 epoll 实例 epollfd 中，并设置监听的事件类型。参数说明：
	// epollfd：epoll 实例的文件描述符。
	// fd：需要添加到 epoll 监听的文件描述符，通常是一个网络连接的套接字。
	// one_shot：是否启用 EPOLLONESHOT。如果为 true，意味着一次性事件触发后该事件会自动从
	// epoll中移除，避免事件重复触发。
	// TRIGMode：触发模式，通常表示事件的工作模式，例如是否采用边缘触发（ET）模式。

	// 信号处理函数
	static void sig_handler(int sig);

	// 设置信号函数
	void addsig(int sig, void(handler)(int), bool restart = true);
	// 通过调用这个函数，可以将一个信号（sig）与信号处理函数（handler）绑定。
	// sig：要处理的信号（如 SIGALRM、SIGINT 等）。
	// handler：信号发生时调用的处理函数，void(handler)(int) 表示一个接收 int
	// 参数并返回void的函数指针。
	// restart：默认为true，表示是否在处理信号后重启系统调用，通常用来处理阻塞的系统调用。

	// 定时处理任务，重新定时以不断触发SIGALRM信号
	void timer_handler();

	void show_error(int connfd, const char *info);

   public:
	static int *
		u_pipefd;  // 用于保存管道的文件描述符。在高并发程序中，管道通常用来进行进程间通信，或将信号传递到主程序或事件循环中。

	sort_timer_lst m_timer_lst;	 // 升序的定时器链表

	static int
		u_epollfd;	// 保存 epoll
					// 实例的文件描述符。程序通过该文件描述符来管理和监听多个文件描述符（如套接字）的事件。通常在事件驱动的网络服务器中，u_epollfd
					// 被用来通过 epoll 实现高效的 I/O 多路复用。

	int m_TIMESLOT;	 // 定时器的时间间隔
};

void cb_func(client_data *user_data);

#endif
