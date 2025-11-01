#include "webserver.h"

WebServer::WebServer()
{
	// http_conn类对象
	users = new http_conn[MAX_FD];	// 单个用户多个连接

	// root文件夹路径
	char server_path[200];

	getcwd(server_path, 200);  // 获取当前工作目录（Current Working Directory）的路径（绝对路径）

	// 拼接出root的绝对路径
	char root[6] = "/root";
	m_root = (char *) malloc(strlen(server_path) + strlen(root) + 1);
	strcpy(m_root, server_path);
	strcat(m_root, root);

	// 定时器
	users_timer = new client_data[MAX_FD];	// 定时器和地址及套接字结构体
}

WebServer::~WebServer()
{
	close(m_epollfd);
	close(m_listenfd);
	close(m_pipefd[1]);
	close(m_pipefd[0]);

	delete[] users;
	delete[] users_timer;

	delete m_pool;
}

void WebServer::init(int port, string user, string passWord, string databaseName, int log_write,
					 int opt_linger, int trigmode, int sql_num, int thread_num, int close_log,
					 int actor_model)
{
	m_port = port;	// 主机端口号

	m_user = user;					// 登陆数据库用户名
	m_passWord = passWord;			// 登陆数据库密码
	m_databaseName = databaseName;	// 使用数据库名

	m_sql_num = sql_num;		// 数据库允许的连接数
	m_thread_num = thread_num;	// 允许的线程数

	m_log_write = log_write;  // 日志写的模式

	m_OPT_LINGER = opt_linger;	// 是否优雅的关闭连接

	m_TRIGMode = trigmode;	// 触发模式：表示 epoll 的工作模式

	m_close_log = close_log;  // 是否关闭日志

	m_actormodel = actor_model;	 // 并发模型
}

void WebServer::trig_mode()	 // 触发模式设置
{
	// LT + LT
	if (0 == m_TRIGMode)
	{
		m_LISTENTrigmode = 0;  // 监听套接字（即服务器用来 accept() 新连接的 socket）的触发模式。
		m_CONNTrigmode = 0;	   // 已建立连接的 socket（即客户端通信的连接）的触发模式。
	}
	// LT + ET
	else if (1 == m_TRIGMode)
	{
		m_LISTENTrigmode = 0;
		m_CONNTrigmode = 1;
	}
	// ET + LT
	else if (2 == m_TRIGMode)
	{
		m_LISTENTrigmode = 1;
		m_CONNTrigmode = 0;
	}
	// ET + ET
	else if (3 == m_TRIGMode)
	{
		m_LISTENTrigmode = 1;
		m_CONNTrigmode = 1;
	}
}
// LT（Level Trigger）	水平触发
// 默认模式。只要缓冲区中有数据，就会不断触发事件。实现简单，但效率略低。
// ET（Edge Trigger） 边缘触发 只在状态变化时触发一次。更高效，但要求使用 非阻塞IO 并一次性读
// /写完数据，否则可能丢事件。

void WebServer::log_write()
{
	if (0 == m_close_log)  // 等于0代表没有关闭日志写，于是进行日志的初始化
	{
		// 初始化日志
		if (1 == m_log_write)
			Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000,
									  800);	 // 最长日志队列为800
											 // 异步日志
		else
			Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000,
									  0);  // 最长日志队列为0
										   // 同步日志
	}
}

void WebServer::sql_pool()	// 初始化数据库及连接
{
	// 初始化数据库连接池
	m_connPool = connection_pool::GetInstance();
	m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);
	// 3306为数据库端口

	// 初始化数据库读取表
	users->initmysql_result(m_connPool);
}

void WebServer::thread_pool()  // 创建线程池
{
	// 线程池
	m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
	// 此处线程池max_request取默认值10000
}

void WebServer::eventListen()
{
	// 服务器的监听初始化，主要负责：
	// 创建监听套接字、绑定端口、启动监听、设置 epoll 事件表、注册信号处理函数等。

	// 网络编程基础步骤
	m_listenfd = socket(PF_INET, SOCK_STREAM, 0);  // 创建一个 TCP 套接字（IPv4）。
												   // PF_INET：使用 IPv4 协议族。
												   // SOCK_STREAM：流式套接字，对应 TCP。
												   // 0 表示让系统自动选择默认协议。
	// 在 PF_INET + SOCK_STREAM 的组合下，系统会自动选择 TCP。

	assert(m_listenfd >= 0);  // 确保创建成功，否则程序直接中止。

	// 优雅关闭连接
	if (0 == m_OPT_LINGER)
	{
		struct linger tmp = {0, 1};	 // 立即关闭连接，丢弃未发送数据（默认方式）
		setsockopt(
			m_listenfd, SOL_SOCKET, SO_LINGER, &tmp,
			sizeof(tmp));  // 给指定的套接字（socket）设置某种选项（option），用来控制该socket
						   // 的行为。 m_listenfd：套接字文件描述符（监听socket）。
						   // SOL_SOCKET：选项所在层（socket 层）。
		// SO_LINGER：设置或查询关闭（close() / shutdown()）时 socket 的 linger 行为。
		// 最后两个参数是传入 struct linger 的地址与大小。
	}
	else if (1 == m_OPT_LINGER)
	{
		struct linger tmp = {1, 1};	 // 优雅关闭连接，等待最多 1 秒把数据发送完再关闭
		setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
	}

	// struct linger
	// {
	// int l_onoff;   //是否开启 linger 行为（非 0 表示开启，0 表示关闭）。
	// 	int l_linger;  // linger 超时时间（单位：秒）
	// };

	int ret = 0;

	struct sockaddr_in address;
	// sockaddr_in 是定义在 <netinet/in.h> 中的一个结构体，用来描述 IPv4 地址与端口（用于
	// bind、connect、recvfrom 等套接字调用）。
	// sa_family_t sin_family; // 地址族（例如 AF_INET）
	// in_port_t sin_port;	 // 16-bit 端口（网络字节序）
	// struct in_addr sin_addr;  // IPv4 地址（32-bit，网络字节序，大端序）

	bzero(&address, sizeof(address));  // 将 address 结构体的所有字节清零（把所有字段初始化为
									   // 0），以避免结构中存在未定义的“垃圾值”。

	address.sin_family = AF_INET;  // 设置为IPv4地址族

	address.sin_addr.s_addr = htonl(INADDR_ANY);
	// INADDR_ANY
	// 是一个宏，数值上等于0，意味着把套接字绑定到主机的所有可用网络接口（比如本地回环、以太网、Wi-Fi
	// 等）。
	//  htonl()：Host TO Network Long（将 32
	//  位整数从主机字节序转换为网络字节序）。网络字节序是大端（big-endian）。
	//  在小端主机（如 x86）上，这一步会把字节顺序翻转为网络序；在大端主机上这通常是 no-op(no
	//  operation)。

	address.sin_port = htons(m_port);
	// htons()：Host TO Network Short（把 16 位整数从主机字节序转换为网络字节序）。

	int flag = 1;
	setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
	// SO_REUSEADDR：允许重用本地地址/端口
	// flag 其实是一个“开关值”（布尔参数），它的作用取决于所设置的套接字选项（此处是
	// SO_REUSEADDR）
	// flag 是选项的“值”，告诉内核该功能是开还是关
	// 当 flag = 1 时：表示 启用 这个选项（即允许地址重用）。
	// 当 flag = 0 时：表示 关闭 这个选项（即禁止地址重用）。

	// flag 的实质意义（对于 SO_REUSEADDR）
	// 当 flag = 1 时，允许套接字在以下情况下“重用”地址：
	// 服务器刚刚退出（TCP连接还处于 TIME_WAIT 状态），新程序仍然可以立即在同一个端口上 bind；
	// 这样可以避免 “Address already in use” 错误；
	// 特别常见于服务器重启场景。

	ret = bind(m_listenfd, (struct sockaddr *) &address, sizeof(address));
	// 把套接字 m_listenfd 绑定到本地地址 address（IP +
	// 端口）。只有绑定之后，内核才知道该套接字要监听哪个本地地址/端口。

	assert(ret >= 0);

	ret = listen(m_listenfd, 5);
	// 把已绑定的套接字 m_listenfd 转换为监听状态，内核开始排队等待连接请求。这个套接字现在可以被
	// accept() 用来接受客户端连接
	// 5：backlog（未完成连接或已完成连接但尚未被 accept 取走的套接字队列长度上限）
	// backlog
	// 指示内核在队列中能保持的未处理连接数上限，超过后新的连接请求可能被拒绝或客户端可能收到连接错误。

	assert(ret >= 0);

	utils.init(TIMESLOT);  // 设置定时器触发的时间间隔

	// epoll创建内核事件表
	epoll_event events[MAX_EVENT_NUMBER];
	// 声明一个 events 数组，用来 接收 epoll_wait 返回的事件。
	// MAX_EVENT_NUMBER 是程序中定义的最大同时监听事件数量
	// 当调用 epoll_wait 时，如果有文件描述符（socket）就绪，这个数组就会被填充。

	// struct epoll_event
	// {
	// 	uint32_t events;	// 感兴趣的事件类型，如可读/可写/错误
	// 	epoll_data_t data;	// 用户自定义数据，一般用来保存fd
	// };

	m_epollfd = epoll_create(5);
	// int epoll_create(int size);
	// size 早期版本表示 预估监听的 fd 数量，现代 Linux 已经忽略这个参数（但必须 > 0）。
	// 返回一个 epoll 实例的 文件描述符。出错返回 - 1。

	assert(m_epollfd != -1);

	utils.addfd(m_epollfd, m_listenfd, false,
				m_LISTENTrigmode);	// 将文件描述符 fd 添加到 epoll 实例中
									// m_epollfd：epoll 实例的文件描述符。
	//  m_listenfd：需要添加到 epoll 监听的文件描述符。
	//  one_shot=false：是否启用 EPOLLONESHOT。如果为 true，意味着一次性事件触发后该事件会自动从
	//  epoll中移除，避免事件重复触发。
	//  TRIGMode：触发模式，表示事件的工作模式。0为LT，1为ET

	http_conn::m_epollfd = m_epollfd;

	ret = socketpair(PF_UNIX, SOCK_STREAM, 0,
					 m_pipefd);	 // 创建一对互相连接的套接字（socket），通常用于进程间通信（IPC）。
								 // PF_UNIX指定套接字的协议族（Protocol Family），这里是 UNIX
								 // 域套接字（也称本地套接字），只能在同一台机器的进程间通信。
								 // SOCK_STREAM表示 流式套接字，提供面向连接、可靠、顺序的字节流通信
								 // 协议参数，一般设置为 0，系统会自动选择合适的协议（对于 PF_UNIX +
								 // SOCK_STREAM，通常就是 UNIX TCP 流）。
								 // m_pipefd
								 // 一个整型数组 int m_pipefd[2]，函数执行成功后：
								 // m_pipefd[0] 和 m_pipefd[1] 就是两个互通的套接字描述符。
								 // 从本质上讲，它像一个 双向管道。
								 // 成功返回 0，失败返回 -1

	assert(ret != -1);

	// 虽然m_pipefd是一个双向管道，但在逻辑上当作m_pipefd[1]是写端，m_pipefd[0]是读端
	utils.setnonblocking(m_pipefd[1]);	// 将 m_pipefd[1] 设置为 非阻塞模式。

	utils.addfd(m_epollfd, m_pipefd[0], false, 0);	// 将读端加入epoll监听列表

	utils.addsig(SIGPIPE, SIG_IGN);	 // 忽略 SIGPIPE 信号。
									 // 当你向一个已被对端关闭的 socket 写数据时，内核会向进程发送
	// SIGPIPE，默认行为是终止进程。对于网络服务程序通常不希望进程被这种信号直接杀死。
	// SIG_IGN 是一个 宏常量，定义在头文件 <signal.h> 中。它代表的意思是 “Ignore Signal” ——
	// 忽略信号。

	utils.addsig(SIGALRM, utils.sig_handler,
				 false);  // 为 SIGALRM 注册一个自定义信号处理函数 utils.sig_handler
	// restart：默认为true，表示是否在处理信号后重启系统调用，通常用来处理阻塞的系统调用。此处是false
	// SIGALRM 是由系统提供的 定时器信号。

	utils.addsig(SIGTERM, utils.sig_handler, false);  // 为 SIGTERM 注册 sig_handler
	// SIGTERM 是用来请求程序正常终止的信号

	alarm(TIMESLOT);  // 设置一个定时闹钟：在 TIMESLOT 秒之后内核会向进程发送 SIGALRM。

	// 工具类,信号和描述符基础操作
	Utils::u_pipefd = m_pipefd;
	Utils::u_epollfd = m_epollfd;
}

void WebServer::timer(int connfd, struct sockaddr_in client_address)  // 创建并初始化定时器
{
	users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user,
					   m_passWord, m_databaseName);	 // 初始化http_conn的各项参数

	// 初始化client_data数据
	// 创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
	users_timer[connfd].address = client_address;
	users_timer[connfd].sockfd = connfd;

	util_timer *timer = new util_timer;
	timer->user_data = &users_timer[connfd];
	timer->cb_func = cb_func;

	time_t cur = time(NULL);			 // 获取当前时间
	timer->expire = cur + 3 * TIMESLOT;	 // 过期时间
	users_timer[connfd].timer = timer;
	utils.m_timer_lst.add_timer(timer);
}

// 若有数据传输，则将定时器往后延迟3个单位
// 并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer *timer)
{
	time_t cur = time(NULL);
	timer->expire = cur + 3 * TIMESLOT;
	utils.m_timer_lst.adjust_timer(timer);

	LOG_INFO("%s", "adjust timer once");
}

void WebServer::deal_timer(util_timer *timer, int sockfd)
{
	timer->cb_func(&users_timer[sockfd]);  // 删除epoll，关闭连接

	if (timer)	// 非空指针为true
	{
		utils.m_timer_lst.del_timer(timer);	 // 删除定时器列表中的定时器
	}

	LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

bool WebServer::dealclientdata()  // 读取新连接
{
	struct sockaddr_in client_address;
	socklen_t client_addrlength =
		sizeof(client_address);	 // socklen_t 是一个系统定义的类型（通常是 unsigned int），用来表示
								 // 地址结构体的长度

	if (0 == m_LISTENTrigmode)	// 水平触发LT
	{
		int connfd = accept(m_listenfd, (struct sockaddr *) &client_address,
							&client_addrlength);  // 从监听队列中取出一个已完成连接的客户端
		// sockfd：监听套接字（即 socket() + bind() + listen() 创建好的）。
		// addr：用来保存客户端的地址信息。
		// addrlen：传入时指定 addr 的长度，返回时会被修改为客户端地址的实际长度。
		// 返回值：
		// 成功：返回一个新的 已连接套接字文件描述符（connfd）。
		// 失败：返回 -1，并设置 errno。

		if (connfd < 0)
		{
			LOG_ERROR("%s:errno is:%d", "accept error", errno);
			return false;
		}

		if (http_conn::m_user_count >= MAX_FD)
		{
			utils.show_error(connfd, "Internal server busy");  // 向客户端发送错误并关闭连接
			LOG_ERROR("%s", "Internal server busy");
			return false;
		}

		timer(connfd, client_address);	// 没有出现错误则初始化定时器
	}

	else  // 边缘触发ET
	{
		// 内核只会在“有新连接到来”时通知一次。
		// 所以必须用 while (1) 循环多次 accept()，直到没有新连接（返回错误如 EAGAIN）。
		// 否则可能有新连接未被处理，导致丢失。

		while (1)
		{
			int connfd =
				accept(m_listenfd, (struct sockaddr *) &client_address, &client_addrlength);
			if (connfd < 0)
			{
				LOG_ERROR("%s:errno is:%d", "accept error", errno);
				break;
			}
			if (http_conn::m_user_count >= MAX_FD)
			{
				utils.show_error(connfd, "Internal server busy");
				LOG_ERROR("%s", "Internal server busy");
				break;
			}
			timer(connfd, client_address);
		}

		return false;
	}

	return true;
}

bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)  // 信号处理函数
{
	// timeout：标志是否接收到 SIGALRM（定时信号，通常用于定时器超时检测）。
	// stop_server：标志是否接收到 SIGTERM（终止信号，表示服务器要停止运行）。

	int ret = 0;
	int sig;

	char signals[1024];
	ret = recv(m_pipefd[0], signals, sizeof(signals), 0);  // 读取管道中的信号

	if (ret == -1)	// 读取出错
	{
		return false;
	}
	else if (ret == 0)	// 对端关闭了
	{
		return false;
	}
	else  // 成功读取，ret为实际读到的字节数
	{
		for (int i = 0; i < ret; ++i)  // 循环处理每个信号
		{
			switch (signals[i])
			{
				case SIGALRM:
				{
					timeout = true;
					break;
				}
				case SIGTERM:
				{
					stop_server = true;
					break;
				}
			}
		}
	}
	return true;
}

void WebServer::dealwithread(int sockfd)  // 按并发模式处理事件
{
	util_timer *timer = users_timer[sockfd].timer;

	// reactor
	if (1 == m_actormodel)	// 事件驱动 + 同步I/O
	{
		// Reactor：主线程分发事件给工作线程 → 工作线程执行 I/O → 主线程等待其处理结果。
		if (timer)
		{
			adjust_timer(timer);
		}

		// 若监测到读事件，将该事件放入请求队列
		m_pool->append(users + sockfd, 0);	// 将任务加入线程池请求队列。
											// 第二个参数 0 通常表示读任务。
		// users + sockfd 是 http_conn 对象指针，代表该客户端连接。

		while (true)
		{
			if (1 == users[sockfd].improv)
			{
				if (1 == users[sockfd].timer_flag)
				{
					deal_timer(timer, sockfd);
					users[sockfd].timer_flag = 0;
				}
				users[sockfd].improv = 0;
				break;
			}
		}
	}
	else  // 事件驱动 + 异步I/O
	{
		// 主线程负责完成读操作（I/O），工作线程只负责业务逻辑处理。
		//  proactor
		if (users[sockfd].read_once())
		{
			LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

			// 若监测到读事件，将该事件放入请求队列
			m_pool->append_p(users + sockfd);

			if (timer)
			{
				adjust_timer(timer);
			}
		}
		else  // read_one出错或者对方关闭连接
		{
			deal_timer(timer, sockfd);
		}
	}
}

void WebServer::dealwithwrite(int sockfd)
{
	util_timer *timer = users_timer[sockfd].timer;	// 获得对应连接的定时器

	// reactor
	if (1 == m_actormodel)
	{
		// 主线程分发事件给工作线程 → 工作线程执行 I/O → 主线程等待其处理结果。
		if (timer)
		{
			adjust_timer(timer);  // 修正定时器时间
		}

		m_pool->append(users + sockfd, 1);	// 写事件加入等待队列

		while (true)
		{
			if (1 == users[sockfd].improv)
			{
				if (1 == users[sockfd].timer_flag)
				{
					deal_timer(timer, sockfd);
					users[sockfd].timer_flag = 0;
				}
				users[sockfd].improv = 0;
				break;
			}
		}
	}
	else
	{
		// 主线程负责完成读操作（I/O），工作线程只负责业务逻辑处理。
		//  proactor
		if (users[sockfd].write())
		{
			LOG_INFO("send data to the client(%s)",
					 inet_ntoa(users[sockfd].get_address()->sin_addr));

			if (timer)
			{
				adjust_timer(timer);
			}
		}
		else  // 出错或者非持久化连接
		{
			deal_timer(timer, sockfd);
		}
	}
}

void WebServer::eventLoop()	 // 主事件循环
{
	bool timeout = false;	   // 超时标志位
	bool stop_server = false;  // 停止服务器标志位

	while (!stop_server)  // 服务器主事件循环，不断监听事件直到 stop_server 为真（要求停止服务器）
	{
		int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER,
								-1);  // 阻塞等待内核返回就绪的文件描述符事件。
									  //-1 表示无限等待直到事件发生。
									  // 返回的事件的文件描述符在events数组中
									  // MAX_EVENT_NUMBER 是数组大小；
									  // 返回值	含义
									  // > 0 返回就绪的文件描述符数量（即 events 中有效元素的个数）
		// 0 超时且无事件发生
		// -1 出错，errno 表示错误原因（如 EINTR 被信号中断）

		if (number < 0 &&
			errno != EINTR)	 // 出错且不是被信号中断（EINTR）导致，就记录错误并退出循环。
		{
			LOG_ERROR("%s", "epoll failure");
			break;
		}

		for (int i = 0; i < number; i++)
		{
			int sockfd = events[i].data.fd;

			// 处理新到的客户连接
			if (sockfd == m_listenfd)
			{
				bool flag = dealclientdata();
				if (false == flag) continue;
			}
			else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
			{
				// 服务器端关闭连接，移除对应的定时器
				util_timer *timer = users_timer[sockfd].timer;
				deal_timer(timer, sockfd);
			}
			// 处理信号
			else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
			{
				bool flag = dealwithsignal(timeout, stop_server);
				if (false == flag) LOG_ERROR("%s", "dealclientdata failure");
			}
			// 处理客户连接上接收到的数据
			else if (events[i].events & EPOLLIN)
			{
				dealwithread(sockfd);
			}
			else if (events[i].events & EPOLLOUT)
			{
				dealwithwrite(sockfd);
			}
		}
		if (timeout)
		{
			utils.timer_handler();

			LOG_INFO("%s", "timer tick");

			timeout = false;
		}
	}
}
