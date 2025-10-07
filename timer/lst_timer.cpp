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

void Utils::init(int timeslot)
{
	m_TIMESLOT = timeslot;
}

// 对文件描述符设置非阻塞
int Utils::setnonblocking(int fd)
{
	int old_option = fcntl(fd, F_GETFL);
	int new_option = old_option | O_NONBLOCK;
	fcntl(fd, F_SETFL, new_option);
	return old_option;
}

// 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
	epoll_event event;
	event.data.fd = fd;

	if (1 == TRIGMode)
		event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
	else
		event.events = EPOLLIN | EPOLLRDHUP;

	if (one_shot) event.events |= EPOLLONESHOT;
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
	setnonblocking(fd);
}

// 信号处理函数
void Utils::sig_handler(int sig)
{
	// 为保证函数的可重入性，保留原来的errno
	int save_errno = errno;
	int msg = sig;
	send(u_pipefd[1], (char *) &msg, 1, 0);
	errno = save_errno;
}

// 设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
	struct sigaction sa;
	memset(&sa, '\0', sizeof(sa));
	sa.sa_handler = handler;
	if (restart) sa.sa_flags |= SA_RESTART;
	sigfillset(&sa.sa_mask);
	assert(sigaction(sig, &sa, NULL) != -1);
}

// 定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler()
{
	m_timer_lst.tick();
	alarm(m_TIMESLOT);
}

void Utils::show_error(int connfd, const char *info)
{
	send(connfd, info, strlen(info), 0);
	close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;

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
