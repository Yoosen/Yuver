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

// #define listenfdET //边缘触发非阻塞
#define listenfdLT //水平触发阻塞

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
    users->initmysql_result(connPool);

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
    /*
     *  1. 这对套接字可以用于全双工通信，每一个套接字既可以读也可以写。例如，可以往sv[0]中写，从sv[1]中读；或者从sv[1]中写，从sv[0]中读；
        2. 如果往一个套接字(如sv[0])中写入后，再从该套接字读时会阻塞，只能在另一个套接字中(sv[1])上读成功；
        3. 读、写操作可以位于同一个进程，也可以分别位于不同的进程，如父子进程。
        如果是父子进程时，一般会功能分离，一个进程用来读，一个用来写。
        因为文件描述副sv[0]和sv[1]是进程共享的，所以读的进程要关闭写描述符,反之，写的进程关闭读描述符。
     */
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);
    // 设置管道写端非阻塞
    setnonblocking(pipefd[1]);
    // 设置管道读端为 ET 非阻塞，并添加到 epoll 内核事件表
    addfd(epollfd, pipefd[0], false);

    addsig(SIGALRM, sig_handler, false);
    addsig(SIGTERM, sig_handler, false);
    bool stop_server = false;

    // 每个 user (http请求)对应的timer
    client_data *user_timer = new client_data[MAX_FD];

    bool timeout = false;
    // 每隔 TIMESLOT 时间触发 SIGALRM 信号
    alarm(TIMESLOT);

    while(!stop_server) {
        /* 主线程调用epoll_wait等待一组文件描述符上的事件，并将当前所有就绪的epoll_event复制到events数组中 */
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if(number < 0 && errno != EINTR) {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        /* 然后我们遍历这一数组以处理这些已经就绪的事件 */
        for(int i = 0; i < number; ++i) {
            // 事件表中就绪的socket文件描述符
            int sockfd = events[i].data.fd;

            // 处理新到达的客户连接
            if(sockfd == listenfd) {    // 当listen到新的用户连接，listenfd上则产生就绪事件
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
#ifdef listenfdLT
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlength);
                if(connfd < 0) {
                    LOG_ERROR("%s:errno is:%d", "accept error", errno);
                    continue;
                }
                if(http_conn::m_user_count >= MAX_FD) {
                    show_error(connfd, "Internal server busy")；
                    LOG_ERROR("%s", "Internal server busy");
                    continue;
                }

                users[connfd].init(connfd, client_address);

                // 初始化 client_data 数据
                //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
                users_timer[connfd].address = client_address;
                user_timer[connfd].sockfd = connfd;
                util_timer *timer = new util_timer;
                timer->user_data = &users_timer[connfd];
                timer->cbfunc = cb_func;
                time_t cur = timer(NULL);
                timer->expire = cur + 3 * TIMESLOT;
                users_timer[connfd].timer = timer;
                timer_lst.add_timer(timer);
#endif
            }

            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 如有异常，则直接关闭客户连接，并删除该用户的timer
                //服务器端关闭连接，移除对应的定时器
                util_timer *timer = user_timer[sockfd].timer;
                // 关闭连接
                timer->cb_func(&users_timer[sockfd]);

                // 移除对应的定时器
                if(timer) {
                    timer_lst.del(timer);
                }
            }

            // 处理信号
            else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN)) {// 与 都是 EPOLLIN 才能是EPOLLIN，否则保持原样
                int sig;
                char signals[1024];
                ret = recv(pipefd[0], signals, sizeof(signals), 0);

                if(ret == -1) {
                    continue;
                }
                else if (ret == 0) {
                    continue;;
                }
                else {
                    for(int i = 0; i < ret; ++i) {
                        switch (signals[i]) {
                            case SIGALRM: {
                                timeout = true;
                                break;
                            }
                            case SIGTERM: {
                                stop_server = true;
                            }
                        }
                    }
                }
            }

            //处理客户连接上接收到的数据
            /* 当这一sockfd上有可读事件时，epoll_wait通知主线程。*/
            else if (events[i].events & EPOLLIN) {
                util_timer *timer = users_timer[sockfd].timer;
                if(users[sockfd].read_once()) {
                    LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
                    Log::get_instance()->flush();

                    // 若检测到读时间，将该事件放入请求队列
                    pool->append(users + sockfd);

                    //若有数据传输，则将定时器往后延迟3个单位
                    //并对新的定时器在链表上的位置进行调整
                    if(timer) {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        LOG_INFO("%s", "adjust timer once");
                        Log::get_instance()->flush();
                        timer_lst.adjust_timer(timer);
                    }
                }
                else {
                    // 断开连接，移除定时器
                    timer->cb_func(&user_timer[sockfd]);
                    if(timer) {
                        timer_lst.del_timer(timer);
                    }
                }
            }

             /* 当这一sockfd上有可写事件时，epoll_wait通知主线程。主线程往socket上写入服务器处理客户请求的结果 */
             else if(events[i].events & EPOLLOUT) {
                util_timer *timer = user_timer[sockfd].timer;
                if(user[sockfd].write()) {
                    LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr);
                    Log::get_instance()->flush();

                    //若有数据传输，则将定时器往后延迟3个单位
                    //并对新的定时器在链表上的位置进行调整
                    if(timer) {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        LOG_INFO("%s", "adjust timer once");
                        Log::get_instance()->flush();
                        timer_lst.adjust_timer(timer);
                    }
                }
                else {
                    timer->cb_func(&users_timer[sockfd]);
                    if(timer) {
                        timer_lst.del_timer(timer);
                    }
                }


             }
       }

        if(timerout) {
            timer_handler();
            timeout = false;
        }
    }
    close(epollfd);
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete pool;
    return 0;

}