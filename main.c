#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./lock/locker.h"
#include "./threadpool/threadpool.h"
#include "./timer/lst_timer.h"
#include "./http/http_conn.h"
#include "./log/log.h"
#include "./CGImysql/sql_connection_pool.h"

#define MAX_FD 65536            // 最大文件描述符
#define MAX_EVENT_NUMBER 1000   // 最大事件数
#define TIMESLOT 5              // 最小超时单位

#define ASYNLOG //异步写日志

#define listenfdET  // 边缘触发非阻塞

//这三个函数在http_conn.cpp中定义，改变链接属性
extern int addfd(int epollfd, int fd, bool one_shot);
extern int remove(int epollfd, int fd);
extern int setnonblocking(int fd);

// 设置定时器相关参数
static int pipefd[2];
static sort_timer_lst timer_lst;
static int epollfd = 0;

// 信号处理函数
void sig_handler(int sig) {
    //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

// 设置信号函数
void addsig(int sig, void(handler)(int), bool restart = true) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if(restart)
        sa.sa_flags |= SA_RESTART;  // SA_RESTART：使被信号打断的系统调用自动重新发起。
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void timer_handler() {
    timer_lst.tick();
    alarm(TIMESLOT);
}

//定时器回调函数，删除非活动连接在socket上的注册事件，并关闭
void cb_func(client_data *user_data) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);

    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
    LOG_INFO("close fd %d", user_data->sockfd);
    Log::get_instance()->flush();
}

void show_error(int connfd, const char *info) {
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int main(int argc, char *argv[]) {
#ifdef ASYNLOG
    Log::get_instance()->init("ServerLog", 2000, 800000, 8);    //异步日志模型
#endif
    if(argc <= 1) {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }
    int port = atoi(argv[1]);

    addsig(SIGPIPE, SIG_IGN);;

    // 创建数据库连接池
    connection_pool *connPool = connection_pool->GetInstance();
    connPool->init("localhost", "root", "7256", "webdb", 3306, 8);

    // 创建线程池
    threadpool<http_conn> *pool = NULL;
    try {
        pool = new threadpool<http_conn>(connPool);
    }
    catch(...) {
        return 1;
    }

    http_conn *users = new http_conn[MAX_FD];
    assert(users);

    // 初始化数据库读取表
    user->initmysql_result(connPool);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htol(INADDR_ANY);
    address.sin_port = htons(port);

    int flag = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(listenfd, 5);
    assert(ret >= 0);

    // 创建内核事件表
    /* 用于存储epoll事件表中就绪事件的event数组 */
    epoll_event events[MAX_EVENT_NUMBER];
    /* 创建一个额外的文件描述符来唯一标识内核中的epoll事件表 */
    epollfd = epoll_create(5);
    assert(epollfd != -1);

    // 主线程往 epoll 内核事件表注册监听 socket 时间，
    // 当listen到新的客户连接，listenfd变为就绪事件
    addfd(epollfd, listenfd, false);
    // 让所有连接记住这个epollfd
    http_conn::m_epollfd = epollfd;

    // 创建管道，注册pipefd[0] 上的可读事件

}