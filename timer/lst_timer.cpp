#include "lst_timer.h"

#include "../http/http_conn.h"

sort_timer_lst::sort_timer_lst()  // 构造函数
{
	head = NULL;
	tail = NULL;
}

sort_timer_lst::~sort_timer_lst()  // 析构函数
{
	util_timer *tmp = head;
	while (tmp)	 // 清空队列
	{
		head = tmp->next;
		delete tmp;
		tmp = head;
	}
}

void sort_timer_lst::add_timer(util_timer *timer)  // 加入定时器
{
	if (!timer)	 // timer是空指针
	{
		return;
	}

	if (!head)	// 初始情况下head和tail均为空，即此时timer为第一个加入列表的元素
	{
		head = tail = timer;
		return;
	}

	if (timer->expire < head->expire)  // 列表中的排序是按照定时器过期时间升序排列的
	{								   // 先到期的排在前面
									   // 此处就是一个头插
		timer->next = head;
		head->prev = timer;
		head = timer;
		return;
	}

	add_timer(timer, head);	 // 如果不满足头插的要求就调用私有函数进行插入
}

void sort_timer_lst::adjust_timer(util_timer *timer)  // 调整定时器位置
{
	if (!timer)	 // 如果指针为空则直接返回
	{
		return;
	}

	util_timer *tmp = timer->next;

	if (!tmp || (timer->expire < tmp->expire))	// 此种情况代表插入的位置是正确的，直接返回即可
	{
		return;
	}

	// 以下情况均是插入位置不正确的情况，如果正确则必然在上一个代码块中被return结束
	if (timer == head)	// timer的位置是列表头
	{
		head = head->next;	// 去除掉timer作为头的影响，即将head只想第二个元素
		head->prev = NULL;
		timer->next = NULL;

		add_timer(timer, head);	 // 重新插入头
	}
	else  // timer的位置不是头
	{
		timer->prev->next = timer->next;  // 前面的后面是后面
		timer->next->prev = timer->prev;  // 后面的前面是前面

		add_timer(timer, timer->next);	// 重新插入
	}
}

void sort_timer_lst::del_timer(util_timer *timer)
{
	if (!timer)	 // timer是空指针
	{
		return;
	}

	if ((timer == head) && (timer == tail))	 // timer是列表中唯一一个元素
	{
		delete timer;
		head = NULL;
		tail = NULL;
		return;
	}

	if (timer == head)	// timer是列表首元素
	{
		head = head->next;
		head->prev = NULL;
		delete timer;
		return;
	}

	if (timer == tail)	// timer是尾元素
	{
		tail = tail->prev;
		tail->next = NULL;
		delete timer;
		return;
	}

	// timer不是头也不是尾
	timer->prev->next = timer->next;
	timer->next->prev = timer->prev;
	delete timer;
}

void sort_timer_lst::tick()	 // 定时器轮询，如果超时就处理掉
{
	if (!head)	// head为空，链表为空
	{
		return;
	}

	time_t cur = time(NULL);  // 当前时间
	util_timer *tmp = head;
	while (tmp)
	{
		if (cur < tmp->expire)	// 由于链表按升序排列，找到一个大于的，后面所有的都是大于的
		{
			break;
		}

		tmp->cb_func(tmp->user_data);  // 处理定时器超时函数，删除连接的套接字的文件描述符
		head = tmp->next;			   // 删除过期的定时器
		if (head)
		{
			head->prev = NULL;
		}
		delete tmp;
		tmp = head;
	}
}

void sort_timer_lst::add_timer(util_timer *timer,
							   util_timer *lst_head)  // 找到timer应该插入的位置并插入
{
	util_timer *prev = lst_head;
	util_timer *tmp = prev->next;
	while (tmp)
	{
		if (timer->expire < tmp->expire)
		{
			prev->next = timer;
			timer->next = tmp;
			tmp->prev = timer;
			timer->prev = prev;
			break;
		}
		prev = tmp;
		tmp = tmp->next;
	}
	if (!tmp)  // 如果遍历完了都没有找到timer应该插入的位置，那么timer就是应该插在末尾，作为尾元素
	{
		prev->next = timer;
		timer->prev = prev;
		timer->next = NULL;
		tail = timer;
	}
}

void Utils::init(int timeslot)	// 初始化定时器的时间间隔
{
	m_TIMESLOT = timeslot;
}

// 对文件描述符设置非阻塞
int Utils::setnonblocking(int fd)
{
	int old_option = fcntl(fd, F_GETFL);  // 获取文件描述符的当前状态标志
	// fcntl() 是一个用于操作文件描述符的系统调用。它可以用来获取或设置文件描述符的状态标志。
	// F_GETFL 是 fcntl 的一个命令，用于获取当前文件描述符（fd）的标志。
	// fcntl(fd, F_GETFL) 会返回文件描述符 fd 的当前标志。返回的 old_option
	// 是一个整数，它包含了当前文件描述符的所有标志位（包括是否设置了 O_NONBLOCK）

	int new_option =
		old_option | O_NONBLOCK;  // 设置 O_NONBLOCK 标志，表示将文件描述符设置为非阻塞模式
	// 这里使用了位运算符 |（按位或）将 O_NONBLOCK 标志设置到现有的标志中。
	// O_NONBLOCK 是一个特殊的标志，表示 非阻塞模式。当文件描述符设置为非阻塞模式时，I /O 操作（如
	// read()或 write()）将不会被阻塞。如果数据不可用，操作会立即返回，并且返回值通常是 -1，并设置
	// errno 为 EAGAIN 或 EWOULDBLOCK，表示操作未完成，但没有错误。 new_option 将包含当前的标志位和
	// O_NONBLOCK 标志位。

	fcntl(fd, F_SETFL, new_option);	 // 使用 F_SETFL 设置新的文件状态标志
	// 使用 fcntl 调用来 设置文件描述符的标志，将其更新为 new_option。即文件描述符 fd
	// 将会被设置为非阻塞模式。
	// F_SETFL 是告诉 fcntl 更新文件描述符的标志

	return old_option;
}

// 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
	epoll_event event;	// 创建 epoll_event 结构体，用来设置文件描述符的事件

	event.data.fd = fd;	 // 设置 epoll_event 结构体中的 data.fd 字段为传入的文件描述符

	// 根据 TRIGMode 参数的值来决定 事件类型
	if (1 == TRIGMode) event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
	// EPOLLIN：表示该文件描述符有数据可读。
	// EPOLLET：表示启用
	// 边缘触发（ET）模式。边缘触发模式下，事件只在状态发生变化时通知一次，程序需要一次性读取所有数据，否则可能错过事件。
	// EPOLLRDHUP：表示监视套接字是否关闭了写端，常用于处理流结束或连接关闭。
	else
		event.events = EPOLLIN | EPOLLRDHUP;

	if (one_shot) event.events |= EPOLLONESHOT;	 // 设置 EPOLLONESHOT 标志
	// EPOLLONESHOT 是一个特殊的标志，表示事件只会触发一次。即，epoll
	// 在事件触发后会自动从事件队列中移除该文件描述符，直到调用 epoll_ctl() 再次注册该文件描述符。

	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);	// 将文件描述符添加到 epoll 事件表
	// 这行代码使用 epoll_ctl() 将 fd（文件描述符）和其对应的事件 event 添加到 epollfd（epoll
	// 实例）的事件表中。
	// EPOLL_CTL_ADD 表示添加新的事件。
	// 这意味着，epoll 开始监视该文件描述符的事件，并在事件发生时通知程序。

	setnonblocking(fd);	 // 设置文件描述符为非阻塞模式
}

// 信号处理函数
void Utils::sig_handler(int sig)
{
	// 为保证函数的可重入性，保留原来的errno，因为send函数可能会修改errno，所以需要保存恢复
	int save_errno = errno;

	// 将信号值发送到管道的写端
	int msg = sig;
	send(u_pipefd[1], (char *) &msg, 1, 0);	 // 将信号编号发送到管道的写端（u_pipefd[1]）。
	// send() 是一个用于发送数据的系统调用，这里将 msg（信号编号）发送到管道中。它的参数解释如下：
	// 第一个参数：u_pipefd[1] 是管道的写端。
	// 第二个参数：(char*) &msg 将 msg 的地址强制转换为 char *，因为 send
	// 函数需要发送的缓冲区是一个字符型指针。 第三个参数：1
	// 表示要发送的字节数。这里发送的是一个字节，存储的是信号编号。 第四个参数：0
	// 是标志，表示不使用额外的标志。
	// 通过这种方式，当信号到达时，信号编号将被写入管道。管道的接收端（通常是另一个线程或进程）可以读取这个信号编号，并根据信号执行相应的操作。

	// 恢复原来的errno
	errno = save_errno;
}

// 设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
	// sig：需要捕获的信号编号（例如 SIGINT, SIGTERM 等）。
	// handler：信号发生时的处理函数，函数原型为 void handler(int)。
	// restart：一个布尔值，指示是否在信号处理后自动重启系统调用。

	struct sigaction sa;  // 创建一个 sigaction 结构体 sa，该结构体用于描述如何处理信号。

	memset(&sa, '\0',
		   sizeof(sa));	 // 使用 memset 将 sa
						 // 结构体的内容清零，确保没有任何未初始化的字段，防止潜在的内存错误。

	sa.sa_handler = handler;  // 设置 sa 结构体中的 sa_handler 字段为指定的处理函数（handler）。

	if (restart) sa.sa_flags |= SA_RESTART;
	// 如果 restart 为 true，设置 sa_flags 字段中的 SA_RESTART 标志。SA_RESTART
	// 标志的作用是：当信号处理完成后，自动重启被中断的系统调用（如 read、write
	// 等）。如果不设置这个标志，中断的系统调用将返回错误并设置 EINTR。

	sigfillset(
		&sa.sa_mask);  // 使用 sigfillset 将信号屏蔽集 sa_mask
					   // 填充为所有信号，即在处理当前信号时，其他信号会被阻塞，直到信号处理函数返回。

	assert(sigaction(sig, &sa, NULL) != -1);
	// 使用 sigaction 函数注册信号处理器。
	// 第一个参数是信号编号 sig。
	// 第二个参数是我们设置好的 sa 结构体，包含了信号的处理方式。
	// 第三个参数设为 NULL，表示不需要获取旧的信号处理方式。
	// assert 用于确保 sigaction 调用成功。如果 sigaction 返回
	// -1，说明注册信号处理器失败，程序会触发断言并终止。
}

// 定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler()
{
	m_timer_lst.tick();	 // 处理列表中的定时器
	alarm(m_TIMESLOT);	 // alarm() 是一个标准的 Unix
						 // 系统调用，用于设置一个定时器，使得在指定的秒数后触发 SIGALRM 信号。
	// m_TIMESLOT 是一个定时器时间间隔，通常表示信号触发的频率（单位是秒）。例如，m_TIMESLOT
	// 的值可能是 1，表示每隔 1 秒就会触发一次 SIGALRM 信号。
	// alarm() 设置了一个定时器，当定时器倒计时结束时，系统会向当前进程发送
	// SIGALRM 信号，从而触发对应的信号处理函数。
}

void Utils::show_error(int connfd, const char *info)
{  // connfd: 这是一个套接字描述符，表示与客户端的连接。

	send(connfd, info, strlen(info), 0);
	// send 函数会将 info 中的错误信息发送到通过 connfd 连接的客户端。
	// strlen(info) 表示要发送的信息的长度。
	// 0 表示没有特殊标志，普通的发送操作。

	close(connfd);
	// 发送完错误信息后，调用 close
	// 关闭与客户端的连接。这个操作释放了文件描述符，断开了与客户端的通信。
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;  // 前向声明，避免循环引用

void cb_func(client_data *user_data)  // 回调函数，处理定时器超时的情况
{
	epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd,
			  0);  // 用于管理 epoll 实例的函数，用于注册、修改或删除文件描述符（套接字）在 epoll
				   // 中的事件。
	// Utils::u_epollfd：epoll 文件描述符，表示已经创建的 epoll 实例。
	// EPOLL_CTL_DEL：表示删除操作。告诉 epoll 从监视列表中移除指定的文件描述符，即不再监听
	// user_data->sockfd：这是 client_data
	// 结构体中的套接字描述符。它表示一个网络连接的文件描述符，epoll_ctl
	// 将不再监听这个套接字的事件。
	// 0：这是事件参数，删除操作不需要特别的事件配置，所以这个值通常设置为 0

	assert(user_data);	// 调试模式下确保user_data指针不为空，避免在后续操作中对空指针进行访问。

	close(user_data->sockfd);

	http_conn::m_user_count--;
}
