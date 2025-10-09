#include "http_conn.h"

#include <mysql/mysql.h>

#include <fstream>

// 定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form =
	"Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;
map<string, string> users;

void http_conn::initmysql_result(connection_pool *connPool)	 // 从数据库中获得用户名和密码
{
	// 先从连接池中取一个连接
	MYSQL *mysql = NULL;
	connectionRAII mysqlcon(&mysql, connPool);

	// 在user表中检索username，passwd数据，浏览器端输入
	if (mysql_query(mysql, "SELECT username,passwd FROM user"))
	{
		LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
	}

	// 从表中检索完整的结果集
	MYSQL_RES *result = mysql_store_result(
		mysql);	 // 将查询结果存储到 MYSQL_RES 结构中，这个结构包含了结果集的所有数据。

	// 返回结果集中的列数
	int num_fields = mysql_num_fields(result);

	// 返回所有字段结构的数组
	MYSQL_FIELD *fields = mysql_fetch_fields(result);
	// 返回一个指向 MYSQL_FIELD 数组的指针，每个 MYSQL_FIELD
	// 结构体代表一个字段的信息（比如字段名、数据类型等）。

	// 从结果集中获取下一行，将对应的用户名和密码，存入map中
	while (MYSQL_ROW row = mysql_fetch_row(result))
	{
		string temp1(row[0]);
		string temp2(row[1]);
		users[temp1] = temp2;
	}
}

// 对文件描述符设置非阻塞
int setnonblocking(int fd)
{
	int old_option = fcntl(fd, F_GETFL);
	int new_option = old_option | O_NONBLOCK;
	fcntl(fd, F_SETFL, new_option);
	return old_option;
}

// 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
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

// 从内核时间表删除描述符
void removefd(int epollfd, int fd)
{
	epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
	close(fd);
}

// 将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev, int TRIGMode)
{
	epoll_event event;
	event.data.fd = fd;

	if (1 == TRIGMode)
		event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
	else
		event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

	epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
	// EPOLL_CTL_MOD 表示修改操作。此时，epoll_ctl 将更新 fd 在 epollfd 中的监听事件为新的 event。
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

// 关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close)
{  // 此处将real_close认为是预留接口（当前的代码似乎没有区分真假关闭）
	if (real_close && (m_sockfd != -1))
	{
		printf("close %d\n", m_sockfd);
		removefd(m_epollfd, m_sockfd);
		m_sockfd = -1;
		m_user_count--;
	}
}

// 初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode, int close_log,
					 string user, string passwd, string sqlname)
{
	m_sockfd = sockfd;	// 套接字描述符
	m_address = addr;	// 客户端地址

	addfd(m_epollfd, sockfd, true,
		  m_TRIGMode);	// 将 sockfd（套接字）添加到 epoll 事件中，用于事件通知。

	m_user_count++;	 // 连接的用户数量加一

	// 当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
	doc_root = root;  // 设置网站的根目录路径。

	m_TRIGMode =
		TRIGMode;  // 设置触发模式，TRIGMode 传入的值用于设置如何处理事件，水平或者边缘触发……

	m_close_log = close_log;  // 是否关闭日志

	strcpy(sql_user, user.c_str());
	strcpy(sql_passwd, passwd.c_str());
	strcpy(sql_name, sqlname.c_str());

	init();
}

// 初始化新接受的连接
// check_state默认为分析请求行状态
void http_conn::init()
{
	mysql = NULL;  // 中断数据库连接

	bytes_to_send = 0;

	bytes_have_send = 0;

	m_check_state = CHECK_STATE_REQUESTLINE;  // 将解析状态机设置为 解析请求行（Request Line）
											  // 状态。
											  // HTTP 请求的解析通常分为几个阶段：
											  // 请求行（Request Line）
											  // 请求头（Request Header）
											  // 请求体（Request Body）
											  // 这里默认从第一阶段开始。

	m_linger = false;  // 设置是否保持连接（即 Connection: keep-alive）为默认关闭。
					   // 若客户端请求了 keep - alive，后续解析时会改成 true。

	m_method = GET;	 // 设置默认请求方法为 GET。

	m_url = 0;
	m_version = 0;
	m_content_length = 0;
	m_host = 0;

	m_start_line = 0;  // 正在解析的行的起始位置

	m_checked_idx = 0;

	m_read_idx = 0;

	m_write_idx = 0;

	cgi = 0;

	m_state = 0;

	timer_flag = 0;

	improv = 0;	 // 改进标志位（常用于同步或线程池通信时标记该连接的处理进展）

	// 清空缓冲区
	memset(m_read_buf, '\0', READ_BUFFER_SIZE);
	memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);

	// 清空文件路径
	memset(m_real_file, '\0', FILENAME_LEN);
}

// 从状态机，用于分析出一行内容
// 返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
http_conn::LINE_STATUS http_conn::parse_line()
{
	// m_read_buf	读取缓冲区，保存从 socket 读来的 HTTP 请求数据
	// m_read_idx 当前已读入缓冲区的字节总数
	// m_checked_idx 当前正在检查的下标（即解析进度）

	char temp;
	for (; m_checked_idx < m_read_idx;
		 ++m_checked_idx)  // 读入的字节数比已经检查过的字节数多，则继续检查
	{
		temp = m_read_buf[m_checked_idx];  // 取出要检查的字节

		if (temp == '\r')  // 当前为'\r'
		{
			if ((m_checked_idx + 1) ==
				m_read_idx)	 // 一个是下标，一个是长度。此处为后面的内容还没有读入
				return LINE_OPEN;
			else if (m_read_buf[m_checked_idx + 1] == '\n')	 // 下一个为'\n'
			{												 //'\r\n'组成http报文的行结尾
				m_read_buf[m_checked_idx++] = '\0';			 // 将'\r'替换为'\0'
				m_read_buf[m_checked_idx++] = '\0';			 // 将'\n'替换为'\0'

				return LINE_OK;	 // 行解析完成
			}

			return LINE_BAD;
		}
		else if (temp == '\n')	// 当前字节为'\n'
		{
			if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')	 // 前一个字节是'\r'
			{  //'\r\n'组成http报文的行结尾

				m_read_buf[m_checked_idx - 1] = '\0';
				m_read_buf[m_checked_idx++] = '\0';

				return LINE_OK;
			}

			return LINE_BAD;  // 其它情况都是语法错误
		}
	}

	return LINE_OPEN;  // 全部检查完了都没有\r\n，说明这一行没有读完，仍然在处理中
}

// 循环读取客户数据，直到无数据可读或对方关闭连接
// 非阻塞ET工作模式下，需要一次性将数据读完
bool http_conn::read_once()
{
	if (m_read_idx >= READ_BUFFER_SIZE)	 // 缓冲区没有空间，直接返回false
	{
		return false;
	}

	int bytes_read = 0;

	// LT读取数据
	if (0 == m_TRIGMode)
	{
		bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
		// 返回类型 ssize_t：≥0 表示读到的字节数；0 表示对端已正常关闭连接；-1 表示出错并设置 errno
		// m_sockfd —— socket 文件描述符（要从哪个连接读数据）。
		// m_read_buf +m_read_idx —— 写入缓冲区的起始地址（从 m_read_buf 的第 m_read_idx
		// 个字节开始写）。 READ_BUFFER_SIZE -m_read_idx ——
		// 能写入的最大字节数（缓冲区剩余容量）。保证不会越界写入。
		// 0 —— flags 为 0，表示使用默认行为（不传特殊标志）。

		m_read_idx += bytes_read;

		if (bytes_read <= 0)
		{
			return false;
		}

		return true;
	}
	// ET读数据
	else
	{
		while (true)
		{
			bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);

			if (bytes_read == -1)  // 返回-1代表读取失败
			{
				if (errno == EAGAIN || errno == EWOULDBLOCK) break;
				// EAGAIN	“资源暂时不可用”	读操作时，当前没有数据可读（非阻塞模式）
				// EWOULDBLOCK	“如果继续操作，会阻塞当前调用” 行为与 EAGAIN 相同

				return false;
			}
			else if (bytes_read == 0)  // 返回 0 表示 对方关闭了连接（TCP FIN）
			{
				return false;
			}

			m_read_idx += bytes_read;
		}
		return true;
	}
}

// 解析http请求行，获得请求方法，目标url及http版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
	m_url = strpbrk(text, " \t");  // 在请求行中查找第一个空格或制表符（\t），返回其指针。

	if (!m_url)	 // 如果没有找到，则请求不合法
	{
		return BAD_REQUEST;
	}

	*m_url++ = '\0';
	// 把第一个空格改成字符串结束符 '\0'，此时：
	// text 变成 "GET"
	// m_url 指向 "/index.html HTTP/1.1"

	char *method = text;

	if (strcasecmp(method, "GET") == 0)	 // strcasecmp：忽略大小写比较字符串。
		m_method = GET;
	else if (strcasecmp(method, "POST") == 0)
	{
		m_method = POST;

		cgi = 1;  // 表示该请求会涉及到动态内容处理
	}
	else
		return BAD_REQUEST;	 // 其它均被视为不合法的请求

	m_url += strspn(m_url, " \t");	// 清除多余的分隔符
	// strspn(m_url, " \t") 计算开头连续空格或制表符的数量；

	m_version = strpbrk(m_url, " \t");

	if (!m_version) return BAD_REQUEST;	 // 没有version，非法请求

	*m_version++ = '\0';					// 拆出 URL 与 HTTP 版本号
	m_version += strspn(m_version, " \t");	// 清除多余的分隔符

	if (strcasecmp(m_version, "HTTP/1.1") != 0) return BAD_REQUEST;	 // 只接受HTTP/1.1版本

	if (strncasecmp(m_url, "http://", 7) == 0)	// 去掉http://头
	{											// 在比较字符串时忽略大小写，最多比较前7个字符。
		m_url += 7;
		m_url = strchr(m_url, '/');
	}

	if (strncasecmp(m_url, "https://", 8) == 0)
	{
		m_url += 8;
		m_url = strchr(m_url, '/');
		// 查找指定字符在字符串中的第一次出现位置，返回指针
	}

	if (!m_url || m_url[0] != '/') return BAD_REQUEST;	// 没找到

	// 当url为/时，显示判断界面
	if (strlen(m_url) == 1) strcat(m_url, "judge.html");
	// 字符串连接函数，用于将一个字符串附加到另一个字符串的末尾。
	// 如果只是单个斜杠 /，说明请求根目录，则默认跳转到 judge.html；
	// （即访问主页时显示默认页面）

	m_check_state =
		CHECK_STATE_HEADER;	 // 将解析状态机的状态改为 CHECK_STATE_HEADER（进入“解析请求头”阶段）

	return NO_REQUEST;
}

// 解析http请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
	if (text[0] == '\0')  // 当前行是空行
	{
		if (m_content_length != 0)	// 存在请求体
		{
			m_check_state = CHECK_STATE_CONTENT;

			return NO_REQUEST;	// 表示请求报文没有解析完
		}

		return GET_REQUEST;	 // 不存在请求体，遇到空行则代表完成了请求头的解析（请求报文解析完成）
	}
	else if (strncasecmp(text, "Connection:", 11) == 0)
	{  // 解析的行为头的连接信息（是否持续连接）
		text += 11;
		text += strspn(text, " \t");
		if (strcasecmp(text, "keep-alive") == 0)
		{
			m_linger = true;
		}
	}
	else if (strncasecmp(text, "Content-length:", 15) == 0)
	{  // 解析请求体长度
		text += 15;
		text += strspn(text, " \t");
		m_content_length = atol(text);
		// atol() 会从字符串 str
		// 的开头开始读取，跳过空格等空白符，然后把连续的数字字符解析为一个长整数（long），直到遇到第一个非数字字符为止。
	}
	else if (strncasecmp(text, "Host:", 5) == 0)
	{  // 解析主机行（服务器域名）
		text += 5;
		text += strspn(text, " \t");
		m_host = text;
	}
	else
	{
		LOG_INFO("oop!unknow header: %s", text);  // 其他行不解析
	}

	return NO_REQUEST;	// 不是空行则代表未解析完成，状态为继续解析
}

// 判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
	if (m_read_idx >= (m_content_length + m_checked_idx))
	{
		// m_read_idx：当前总共读入的字节数（包括请求行、头部和内容）。
		// m_checked_idx：已经解析过的部分的末尾位置（一般在请求头结束处）。
		// m_content_length：请求体的长度（从头部字段 Content -Length : 得到）。

		text[m_content_length] = '\0';

		// POST请求中最后为输入的用户名和密码
		m_string = text;

		return GET_REQUEST;	 // 完成请求报文的解析，变更状态
	}

	return NO_REQUEST;	// 其它情况均为未读取完
}

http_conn::HTTP_CODE http_conn::process_read()
{
	LINE_STATUS line_status = LINE_OK;
	HTTP_CODE ret = NO_REQUEST;
	char *text = 0;
	// LINE_STATUS：行解析的状态（可能是 LINE_OK, LINE_BAD, LINE_OPEN），由 parse_line()
	// 返回，用于检测一行是否完整。
	// HTTP_CODE：HTTP请求整体解析状态。
	// text：指向当前解析到的一行字符串（请求行或请求头等）。

	while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) ||
		   ((line_status = parse_line()) == LINE_OK))
	{
		text = get_line();	// 一次解析一行

		m_start_line = m_checked_idx;

		LOG_INFO("%s", text);  // 打印日志

		switch (m_check_state)	// 根据解析的内容跳转到不同的解析函数
		{
			case CHECK_STATE_REQUESTLINE:  // 解析请求行
			{
				ret = parse_request_line(text);

				if (ret == BAD_REQUEST) return BAD_REQUEST;

				break;
			}
			case CHECK_STATE_HEADER:  // 解析请求头
			{
				ret = parse_headers(text);

				if (ret == BAD_REQUEST)
					return BAD_REQUEST;
				else if (ret == GET_REQUEST)
				{
					return do_request();
				}

				break;
			}
			case CHECK_STATE_CONTENT:  // 解析请求体
			{
				ret = parse_content(text);

				if (ret == GET_REQUEST) return do_request();

				line_status =
					LINE_OPEN;	// 此时代表请求体未解析完成
								// line_status =
								// LINE_OPEN会导致while循环终止，这代表请求报文没有完全被收到，应该等待内容被完整收到以后再进行解析

				break;
			}
			default:
				return INTERNAL_ERROR;	// 其它情况属于内部错误
		}
	}

	return NO_REQUEST;	// 其它终止情况均为未解析完成
}

http_conn::HTTP_CODE http_conn::do_request()
{
	strcpy(m_real_file, doc_root);	// 将文档根目录（doc_root）复制到 m_real_file

	int len = strlen(doc_root);

	// printf("m_url:%s\n", m_url);

	const char *p = strrchr(m_url, '/');  // 查找 URL 中最后一个 / 的位置

	// 处理cgi
	if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
	{
		// 根据标志判断是登录检测还是注册检测
		char flag = m_url[1];

		char *m_url_real = (char *) malloc(sizeof(char) * 200);

		strcpy(m_url_real, "/");

		strcat(m_url_real, m_url + 2);

		strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
		// strncpy 用来将 m_url_real 的内容复制到 m_real_file 中。m_real_file + len 表示从
		// m_real_file 中的 len 位置开始写入数据，防止覆盖掉原来的数据。
		// FILENAME_LEN - len -1 是限制复制的字符数，确保不会超过 m_real_file
		// 的最大长度，并留出空间来存储字符串的结束符（\0）。

		free(m_url_real);  // 释放申请的空间

		// 将用户名和密码提取出来
		// user=123&passwd=123
		char name[100], password[100];

		int i;
		for (i = 5; m_string[i] != '&'; ++i) name[i - 5] = m_string[i];	 // 提取出用户名
		name[i - 5] = '\0';

		int j = 0;
		for (i = i + 10; m_string[i] != '\0'; ++i, ++j) password[j] = m_string[i];	// 提取出密码
		password[j] = '\0';

		if (*(p + 1) == '3')
		{
			// 如果是注册，先检测数据库中是否有重名的
			// 没有重名的，进行增加数据

			char *sql_insert = (char *) malloc(sizeof(char) * 200);

			// sql语句
			strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
			strcat(sql_insert, "'");
			strcat(sql_insert, name);
			strcat(sql_insert, "', '");
			strcat(sql_insert, password);
			strcat(sql_insert, "')");

			if (users.find(name) == users.end())  // 查找是否存在重名
			{									  // 不存在重名，进行注册
				m_lock.lock();					  // 互斥锁

				int res = mysql_query(mysql, sql_insert);  // 执行sql插入
				// 返回值为 0：表示查询成功执行，没有错误。
				// 返回值非 0：表示查询执行失败，具体的错误信息可以通过 mysql_error(mysql) 获取。

				users.insert(pair<string, string>(name, password));	 // 插入到映射表

				m_lock.unlock();  // 解除互斥锁

				// 按照是否出错重定向url
				if (!res)
					strcpy(m_url, "/log.html");
				else
					strcpy(m_url, "/registerError.html");
			}
			else  // 存在重名
				strcpy(m_url, "/registerError.html");
		}
		// 如果是登录，直接判断
		// 若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
		else if (*(p + 1) == '2')
		{
			if (users.find(name) != users.end() && users[name] == password)
				strcpy(m_url, "/welcome.html");
			else
				strcpy(m_url, "/logError.html");
		}
	}

	if (*(p + 1) == '0')  // 注册页面
	{
		char *m_url_real = (char *) malloc(sizeof(char) * 200);

		// 跳转到注册页面，根目录+注册页面构成完整的路径
		strcpy(m_url_real, "/register.html");
		strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

		free(m_url_real);
	}
	else if (*(p + 1) == '1')  // 登录页面
	{
		char *m_url_real = (char *) malloc(sizeof(char) * 200);

		strcpy(m_url_real, "/log.html");
		strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

		free(m_url_real);
	}
	else if (*(p + 1) == '5')  // 图片页面
	{
		char *m_url_real = (char *) malloc(sizeof(char) * 200);

		strcpy(m_url_real, "/picture.html");
		strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

		free(m_url_real);
	}
	else if (*(p + 1) == '6')  // 视频页面
	{
		char *m_url_real = (char *) malloc(sizeof(char) * 200);

		strcpy(m_url_real, "/video.html");
		strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

		free(m_url_real);
	}
	else if (*(p + 1) == '7')  // 粉丝页面
	{
		char *m_url_real = (char *) malloc(sizeof(char) * 200);

		strcpy(m_url_real, "/fans.html");
		strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

		free(m_url_real);
	}
	else  // 其它页面
		strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

	if (stat(m_real_file, &m_file_stat) < 0)
		return NO_RESOURCE;	 // stat 系统调用检查请求的文件是否存在、是否可读、以及是否为目录。
	// 它将文件的元数据（如大小、权限、类型等）存储在 m_file_stat 结构中。如果 stat 返回值小于
	// 0，表示文件不存在或者无法访问。

	if (!(m_file_stat.st_mode & S_IROTH)) return FORBIDDEN_REQUEST;
	// m_file_stat.st_mode 包含文件的权限信息。通过位操作 &
	// 检查文件的权限是否包含对其他用户（S_IROTH）的读取权限。如果没有读取权限，返回
	// FORBIDDEN_REQUEST，表示权限不足，拒绝访问。

	if (S_ISDIR(m_file_stat.st_mode)) return BAD_REQUEST;
	// 用于判断文件是否是一个目录。如果是目录，返回
	// BAD_REQUEST，表示请求错误，因为请求的资源不应该是一个目录。

	int fd = open(m_real_file, O_RDONLY);

	m_file_address = (char *) mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	// mmap 将文件映射到内存中，m_file_stat.st_size
	// 是文件的大小。该文件内容将被映射到内存中的一个区域，供程序读取。这里使用了以下参数：
	// 0：表示让操作系统自动选择映射区域的地址。
	// PROT_READ：表示映射区域的权限为只读。
	// MAP_PRIVATE：表示映射区域是私有的，修改映射区域的内容不会影响原文件。
	// fd：文件描述符，指定要映射的文件。
	// 0：偏移量，通常设为 0，表示从文件开头开始映射。
	// 如果 mmap 调用成功，文件的内容将被加载到内存中，m_file_address 指向该内存区域。

	return FILE_REQUEST;
}

void http_conn::unmap()	 // 解除对映射文件的映射关系
{
	if (m_file_address)	 // 映射地址存在
	{
		munmap(m_file_address,
			   m_file_stat.st_size);  // 使用 munmap 函数解除文件映射，传入文件的地址和大小

		m_file_address = 0;
	}
}

bool http_conn::write()
{
	int temp = 0;

	if (bytes_to_send == 0)	 // 没有字节需要发送
	{
		// 该操作代表数据传输完毕，事件从写入转换为读取
		modfd(m_epollfd, m_sockfd, EPOLLIN,
			  m_TRIGMode);	// 修改 epoll 中的文件描述符 m_sockfd 的事件监听类型。
		// EPOLLIN 是 epoll 监听的事件之一，表示“可读事件”。即当套接字 m_sockfd
		// 上有数据可读时，epoll 会通知程序。
		// 这个标志用于告诉 epoll 监听 m_sockfd 上的数据读取操作。
		// m_TRIGMode 是一个用于表示触发模式的标志

		init();	 // 初始化成员函数

		return true;
	}

	while (1)
	{
		temp = writev(m_sockfd, m_iv, m_iv_count);
		// 使用 writev 函数将 m_iv 中的数据（一个由多个缓冲区组成的数组）写入
		// m_sockfd，即通过套接字发送响应。m_iv_count 是 iov 数组的元素个数。

		// 成功时返回的字节数（ssize_t）：
		// 如果 writev
		// 成功，它会返回实际写入的字节数（ssize_t类型），即它成功写入的总字节数。这可能少于缓冲区的总大小，因为实际写入的字节数取决于操作系统的可用缓冲区和目标套接字的状态。
		// 如果有多个缓冲区（即 iovcnt
		// >1），它会返回成功写入的总字节数。比如，如果有两个缓冲区，每个缓冲区有 100
		// 字节要写入，但实际写入了 150 字节，它会返回 150。 失败时返回
		// -1：如果写操作失败（比如套接字不可用、发生 I /O 错误等），writev 会返回 -1。此时，errno
		// 会被设置为相应的错误码，表示错误的原因。例如，EAGAIN表示资源暂时不可用，需要稍后重试。

		if (temp < 0)  // writev操作失败
		{
			if (errno == EAGAIN)  // 当前无法进行写操作
			{
				modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);  // 修改监听事件
				// 将事件从读事件转换为写事件，即在可以写的时候触发事件

				return true;
			}

			// 其它错误
			unmap();  // 取消文件内存映射

			return false;  // 返回失败
		}

		// 维护已经发送的和将要发送的字节数
		bytes_have_send += temp;
		bytes_to_send -= temp;

		if (bytes_have_send >=
			m_iv[0]
				.iov_len)  // 已经发送的字节数大于第一个缓冲区的字节数（第一个缓冲区已经发送完毕）
		{
			m_iv[0].iov_len = 0;  // 表示这个缓冲区的内容已经完全发送
			m_iv[1].iov_base =
				m_file_address + (bytes_have_send - m_write_idx);  // 确定需要写入的数据从哪里开始
			m_iv[1].iov_len = bytes_to_send;					   // 第二个缓冲区需要写入的长度
		}
		else  // 第一部分缓冲区还没有发送完
		{
			m_iv[0].iov_base = m_write_buf + bytes_have_send;	  // 更新需要发送的部分
			m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;  // 更新需要发送的长度
		}

		if (bytes_to_send <= 0)	 // 数据全部发送完
		{
			unmap();  // 解除文件内存映射

			modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);  // 改为读取事件，等待有数据读取

			if (m_linger)  // 如果是持续性连接
			{
				init();	 // 重新初始化连接

				return true;
			}
			else  // 非持久化连接
			{
				return false;
			}
		}
	}
}

bool http_conn::add_response(const char *format, ...)
{
	if (m_write_idx >= WRITE_BUFFER_SIZE) return false;	 // 检查缓冲区大小是否足够

	// 使用了 C 标准库中的变长参数功能（va_list），初始化一个 arg_list 来访问后续的可变参数。format
	// 是第一个参数，它是格式化字符串。
	va_list arg_list;
	// va_list 是 C
	// 标准库提供的一个类型，用于保存变长参数的处理信息。它类似于一个指针，指向传入的变长参数的内存位置。
	// 在这行代码中，arg_list 是一个变量，声明为类型 va_list，用于存储参数列表的状态。

	va_start(arg_list, format);
	// va_start 是一个宏，用于初始化一个 va_list 类型的变量（在这里是 arg_list）。它的作用是使得
	// arg_list 可以正确地访问函数的变长参数。
	// va_start 宏的第一个参数是一个 va_list
	// 变量（在这里是arg_list），第二个参数是函数的最后一个固定参数（在这里是format）。该宏的作用是找到变长参数的起始位置，并使
	// arg_list 指向变长参数的第一个参数。

	// 在使用变长参数时，format
	// 参数通常用作“格式控制符”，它指定了如何解释后续的变长参数。也就是说，format
	// 作为一个固定参数，决定了后续的可变参数的类型、数量和如何格式化。

	int len =
		vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
	// vsnprintf 函数根据 format 和 arg_list 中的参数，将格式化后的字符串写入到 m_write_buf
	// 中。写入位置是 m_write_buf + m_write_idx，即从缓冲区当前的写入位置开始。写入的最大长度是
	// WRITE_BUFFER_SIZE - 1 - m_write_idx，确保不会溢出。
	// len值为实际写入的字节数

	if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
	{  // 实际写入的字符数）大于或等于剩余空间的大小，就表示写入的数据超出了缓冲区大小。这时，函数会调用
	   // va_end(arg_list) 结束变长参数的处理，并返回 false。

		va_end(arg_list);
		// va_end 的作用是 释放变长参数访问时可能分配的资源，并通知编译器对 va_list 进行最后的清理。

		/*
		资源释放：在某些平台或编译器中，va_list
		可能会分配动态资源，va_end会确保这些资源被正确释放。即使在某些平台上并不需要显式释放内存，调用
		va_end也是一个规范，它确保了编译器的行为符合标准。避免潜在的错误：如果没有调用va_end，可能会导致未定义的行为，或者在多次调用变长参数函数时引发资源泄漏或错误。
		*/

		return false;
	}

	m_write_idx += len;	 // 更新下一次的写入位置

	va_end(arg_list);  // 结束变长数组的处理

	LOG_INFO("request:%s",
			 m_write_buf);	// 日志记录
	// LOG_INFO是一个宏（或函数），它通常用于记录信息级别的日志。这个宏的具体实现会依赖于你使用的日志库或框架。
	// LOG_INFO是一种日志级别，通常表示普通的、重要但不紧急的信息，比如请求的处理、状态更新等。一般情况下，它不会像ERROR
	// 或 WARN 那样表示严重问题，而是简单的操作或数据记录。

	return true;
}

bool http_conn::add_status_line(int status,
								const char *title)	// 将 HTTP 响应的 状态行 添加到响应缓冲区中。
{
	// status：表示 HTTP 状态码（例如 200、404、500 等）。
	// title：表示与状态码相关的状态描述文字（例如"OK"、"Not Found"、"Internal Server Error" 等）。

	return add_response("%s %d %s\r\n", "HTTP/1.1", status,
						title);	 // 将格式化后的状态行添加到响应缓冲区
}

bool http_conn::add_headers(int content_len)
{
	return add_content_length(content_len) && add_linger() && add_blank_line();
}

bool http_conn::add_content_length(int content_len)
{
	return add_response("Content-Length:%d\r\n", content_len);
}

bool http_conn::add_content_type()
{
	return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_conn::add_linger()
{
	return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line()
{
	return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char *content)
{
	return add_response("%s", content);
}

bool http_conn::process_write(HTTP_CODE ret)
{
	switch (ret)
	{
		case INTERNAL_ERROR:
		{
			add_status_line(500, error_500_title);
			add_headers(strlen(error_500_form));
			if (!add_content(error_500_form)) return false;
			break;
		}
		case BAD_REQUEST:
		{
			add_status_line(404, error_404_title);
			add_headers(strlen(error_404_form));
			if (!add_content(error_404_form)) return false;
			break;
		}
		case FORBIDDEN_REQUEST:
		{
			add_status_line(403, error_403_title);
			add_headers(strlen(error_403_form));
			if (!add_content(error_403_form)) return false;
			break;
		}
		case FILE_REQUEST:
		{
			add_status_line(200, ok_200_title);
			if (m_file_stat.st_size != 0)
			{
				add_headers(m_file_stat.st_size);
				m_iv[0].iov_base = m_write_buf;
				m_iv[0].iov_len = m_write_idx;
				m_iv[1].iov_base = m_file_address;
				m_iv[1].iov_len = m_file_stat.st_size;
				m_iv_count = 2;
				bytes_to_send = m_write_idx + m_file_stat.st_size;
				return true;
			}
			else
			{
				const char *ok_string = "<html><body></body></html>";
				add_headers(strlen(ok_string));
				if (!add_content(ok_string)) return false;
			}
		}
		default:
			return false;
	}
	m_iv[0].iov_base = m_write_buf;
	m_iv[0].iov_len = m_write_idx;
	m_iv_count = 1;
	bytes_to_send = m_write_idx;
	return true;
}

void http_conn::process()
{
	HTTP_CODE read_ret = process_read();
	if (read_ret == NO_REQUEST)
	{
		modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
		return;
	}
	bool write_ret = process_write(read_ret);
	if (!write_ret)
	{
		close_conn();
	}
	modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}
