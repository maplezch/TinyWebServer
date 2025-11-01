# 关于此项目
本项目是原作者 [qinguoyi](https://github.com/qinguoyi/TinyWebServer?tab=readme-ov-file) 发布的 **TinyWebServer** 的 fork 版本。

在本文中：
- **原作者** 指 qinguoyi  
- **作者** 指我（一位计算机系的大学生，希望通过解读与修改该项目进行学习）

---
## 学习进度说明

> 当前项目的学习尚未完成，作者仍在抽空进行中。
---

## 已完成的主要工作

1. 对项目代码进行了解读并添加了详细的注释
2. 本地运行成功

---

既然是学习，那么就先写一下作者在本地运行时都遇到了什么问题并进行解答

---

## 项目运行说明与问题记录

---

### 1. 注意事项

> ⚠️ **注意：mysql的客户端和服务器是分开的，需要分别安装**

---

### 2. 初始登录与用户创建

初始安装完成的mysql只有root用户且未设置密码，需要使用

```bash
sudo mysql
```

命令登录。

实际项目需要使用用户名和密码连接数据库。
建议创建新的mysql用户并创建数据库满足项目连接需要（此处作者是在root用户下创建的数据库，所以需要授予新用户该数据库的所有权限）。

---

### 3. build.sh 脚本格式问题

原项目使用的`build.sh`在格式上似乎存在问题，直接执行

```bash
sh ./build.sh
```

会导致 **not found** 警告。

解决方案如下：

```bash
# 安装 dos2unix
sudo apt install dos2unix 

# 转换 build.sh 脚本格式
dos2unix build.sh    

# 清理与重新编译
make clean
sh ./build.sh
```

执行后警告消除:)

---

### 4. 缺少 MySQL 头文件错误

出现错误：

```
fatal error: mysql/mysql.h: No such file or directory
```

这是编译器找不到 MySQL 开发库头文件，需要安装开发库（libmysqlclient-dev）。

```bash
sudo apt install libmysqlclient-dev
```

---

### 5. 段错误 (Segmentation fault)

请阅读原作者所写的 README 中快速运行的指南。
其中“测试前确认已安装MySQL数据库”部分的SQL代码在执行server之前请务必执行过。

项目代码中会读取连接的数据库中的 user 表（用 result 指针指向读取到的内容）。
如果没有 create 这个名为 user 的表，或者没有添加数据（insert），那么在代码运行到获取 result 指向的内容的列数的时候，会变成对一个空指针进行读取内容的操作，导致 **segment fault**。

---

#### 附：调试方法

如果读者跟作者一样没有执行这段 sql 就运行项目并遇到了 segment fault，可以使用 **gdb** 进行调试。命令如下：

```bash
gdb ./server
(gdb) run
```

程序会崩溃，输出类似如下内容：

```
Program received signal SIGSEGV, Segmentation fault.
0x00007ffff7845044 in mysql_num_fields ()
   from /lib/x86_64-linux-gnu/libmysqlclient.so.21
```

可以看到是 `mysql_num_fields()` 函数出了问题。

接着执行：

```
(gdb) bt
```

输出如下：

```
#0  0x00007ffff7845044 in mysql_num_fields ()
   from /lib/x86_64-linux-gnu/libmysqlclient.so.21
#1  0x000055555555912c in http_conn::initmysql_result
   (this=0x7fffe727f018, connPool=0x5555555692a0 <connection_pool::GetInstance()::connPool>)
   at http/http_conn.cpp:39
#2  0x0000555555560fe2 in WebServer::sql_pool (this=0x7ffffffe0500)
   at webserver.cpp:116
#3  0x00005555555582ec in main (argc=1, argv=0x7fffffffdc08)
   at main.cpp:26
```

可以看到是 `http_conn::initmysql_result` 函数调用的 `mysql_num_fields()` 时出了问题。

找到这个地方，检查一下，可以发现指针没有做空指针保护。
根据上下文代码已经能知道错误原因了。**完成! :)**  
（注：此项目的日志输出详细。对于以上问题，可以看到日志中的详细错误记录“2025-11-01 19:07:11.719607 [erro]: SELECT error:Table 'for_tiny_webserver.user' doesn't exist”，从这条记录中也能看出错误原因）

---

## 致谢

以下为原作者 [qinguoyi](https://github.com/qinguoyi/TinyWebServer) 在项目中给出的致谢内容（原文保留）：

> Linux高性能服务器编程，游双著.  
感谢以下朋友的PR和帮助: [@RownH](https://github.com/RownH)，[@mapleFU](https://github.com/mapleFU)，[@ZWiley](https://github.com/ZWiley)，[@zjuHong](https://github.com/zjuHong)，[@mamil](https://github.com/mamil)，[@byfate](https://github.com/byfate)，[@MaJun827](https://github.com/MaJun827)，[@BBLiu-coder](https://github.com/BBLiu-coder)，[@smoky96](https://github.com/smoky96)，[@yfBong](https://github.com/yfBong)，[@liuwuyao](https://github.com/liuwuyao)，[@Huixxi](https://github.com/Huixxi)，[@markparticle](https://github.com/markparticle)，[@blogg9ggg](https://github.com/Blogg9ggg).

---

同时，作者也想特别感谢 qinguoyi 的开源分享，让作者能够在学习该项目并从中受益良多。