#include "webserver.h"

WebServer::WebServer(
    int port, int trigMode, int timeoutMS, bool optLinger, int threadNum) :
    port_(port), openLinger_(optLinger), timeoutMS_(timeoutMS), isClose_(false),
    timer_(new TimerManager()), threadpool_(new ThreadPool(threadNum)), epoller_(new Epoller()) {
    //获取当前工作目录的绝对路径
    srcDir_ = getcwd(nullptr, 256);
    assert(srcDir_);
    //拼接字符串
    strncat(srcDir_, "/resources/", 16);    // 资源文件夹
    HTTPconnection::userCount = 0;          // 静态变量，初始化一次就可以
    HTTPconnection::srcDir = srcDir_;

    initEventMode_(trigMode);               // 事件模式的初始化
    if (!initSocket_()) isClose_ = true;

}

WebServer::~WebServer()
{
    close(listenFd_);
    isClose_ = true;
    free(srcDir_);
}

void WebServer::initEventMode_(int trigMode) {      // 初始时 mode = 3
    listenEvent_ = EPOLLRDHUP;      // 表示读关闭
    connectionEvent_ = EPOLLONESHOT | EPOLLRDHUP;   // 只能有一个线程或进程处理同一个描述符 | 读关闭
    switch (trigMode)
    {
        case 0:
            break;
        case 1:
            connectionEvent_ |= EPOLLET;
            break;
        case 2:
            listenEvent_ |= EPOLLET;
            break;
        case 3:
            listenEvent_ |= EPOLLET;        // ET 读
            connectionEvent_ |= EPOLLET;    // ET 连接
            break;
        default:
            listenEvent_ |= EPOLLET;
            connectionEvent_ |= EPOLLET;
            break;
    }
    HTTPconnection::isET = (connectionEvent_ & EPOLLET);
}

void WebServer::Start()
{
    int timeMS = -1;    // epoll wait timeout==-1就是无事件一直阻塞
    if (!isClose_)
    {
        std::cout << "============================";
        std::cout << "Server Start!";
        std::cout << "============================";
        std::cout << std::endl;
    }
    while (!isClose_)
    {
        if (timeoutMS_ > 0)
        {
            timeMS = timer_->getNextHandle();
        }
        int eventCnt = epoller_->wait(timeMS);  // 无事件一直阻塞
        for (int i = 0; i < eventCnt; ++i)
        {
            int fd = epoller_->getEventFd(i);
            uint32_t events = epoller_->getEvents(i);

            if (fd == listenFd_)    // 就是收到新的 HTTP 请求的时候
            {
                handleListen_();
                //std::cout<<fd<<" is listening!"<<std::endl;
            }
            else if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                assert(users_.count(fd) > 0);
                closeConn_(&users_[fd]);
            }
            else if (events & EPOLLIN) {
                assert(users_.count(fd) > 0);
                handleRead_(&users_[fd]);
                //std::cout<<fd<<" reading end!"<<std::endl;
            }
            else if (events & EPOLLOUT) {
                assert(users_.count(fd) > 0);
                handleWrite_(&users_[fd]);
            }
            else {
                std::cout << "Unexpected event" << std::endl;
            }
        }
    }
}

void WebServer::sendError_(int fd, const char* info)
{
    assert(fd > 0);
    int ret = send(fd, info, strlen(info), 0);
    if (ret < 0)
    {
        //std::cout<<"send error to client"<<fd<<" error!"<<std::endl;
    }
    close(fd);
}

void WebServer::closeConn_(HTTPconnection* client)
{
    assert(client);
    //std::cout<<"client"<<client->getFd()<<" quit!"<<std::endl;
    epoller_->delFd(client->getFd());
    client->closeHTTPConn();
}

void WebServer::addClientConnection(int fd, sockaddr_in addr)
{
    assert(fd > 0);
    users_[fd].initHTTPConn(fd, addr);
    if (timeoutMS_ > 0)
    {
        timer_->addTimer(fd, timeoutMS_, std::bind(&WebServer::closeConn_, this, &users_[fd]));
    }
    epoller_->addFd(fd, EPOLLIN | connectionEvent_);
    setFdNonblock(fd);
}

void WebServer::handleListen_() {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    do {
        int fd = accept(listenFd_, (struct sockaddr*)&addr, &len); // fd 返回一个新的新的套接字和客户端通信
        if (fd <= 0) { return; }
        else if (HTTPconnection::userCount >= MAX_FD) {
            sendError_(fd, "Server busy!");
            //std::cout<<"Clients is full!"<<std::endl;
            return;
        }
        // 得到新的描述符，然后需要将新的描述符和新的描述符对应的连接记录下来
        addClientConnection(fd, addr);
    } while (listenEvent_ & EPOLLET);
}

void WebServer::handleRead_(HTTPconnection* client) {
    assert(client);
    extentTime_(client);
    threadpool_->submit(std::bind(&WebServer::onRead_, this, client));
}

void WebServer::handleWrite_(HTTPconnection* client)
{
    assert(client);
    extentTime_(client);
    threadpool_->submit(std::bind(&WebServer::onWrite_, this, client));
}

void WebServer::extentTime_(HTTPconnection* client)
{
    assert(client);
    if (timeoutMS_ > 0)
    {
        timer_->update(client->getFd(), timeoutMS_);
    }
}

void WebServer::onRead_(HTTPconnection* client)
{
    assert(client);
    int ret = -1;
    int readErrno = 0;
    ret = client->readBuffer(&readErrno);
    //std::cout<<ret<<std::endl;
    if (ret <= 0 && readErrno != EAGAIN) {
        //std::cout<<"do not read data!"<<std::endl;
        closeConn_(client);
        return;
    }
    onProcess_(client);
}

void WebServer::onProcess_(HTTPconnection* client)
{
    if (client->handleHTTPConn()) {
        epoller_->modFd(client->getFd(), connectionEvent_ | EPOLLOUT);
    }
    else {
        epoller_->modFd(client->getFd(), connectionEvent_ | EPOLLIN);
    }
}

void WebServer::onWrite_(HTTPconnection* client) {
    assert(client);
    int ret = -1;
    int writeErrno = 0;
    ret = client->writeBuffer(&writeErrno);
    if (client->writeBytes() == 0) {
        /* 传输完成 */
        if (client->isKeepAlive()) {
            onProcess_(client);
            return;
        }
    }
    else if (ret < 0) {
        if (writeErrno == EAGAIN) {
            /* 继续传输 */
            epoller_->modFd(client->getFd(), connectionEvent_ | EPOLLOUT);
            return;
        }
    }
    closeConn_(client);
}
bool WebServer::initSocket_() {
    int ret;
    struct sockaddr_in addr;
    if (port_ > 65535 || port_ < 1024) {     // 检查端口
        //std::cout<<"Port number error!"<<std::endl;
        return false;
    }
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);
    struct linger optLinger = { 0 };
    if (openLinger_) {
        /* 优雅关闭: 直到所剩数据发送完毕或超时 */
        optLinger.l_onoff = 1;
        optLinger.l_linger = 1;
    }

    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        //std::cout<<"Create socket error!"<<std::endl;
        return false;
    }

    // SO_LINGER 如果选择此选项, close或 shutdown将等到所有套接字里排队的消息成功发送或到达延迟时间后>才会返回. 否则, 调用将立即返回
    // optLinger 接收值
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
    if (ret < 0) {
        close(listenFd_);
        //std::cout<<"Init linger error!"<<std::endl;
        return false;
    }

    int optval = 1;
    /* 端口复用 */
    /* 只有最后一个套接字会正常接收数据。 */
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));
    if (ret == -1) {
        //std::cout<<"set socket setsockopt error !"<<std::endl;
        close(listenFd_);
        return false;
    }

    ret = bind(listenFd_, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0) {
        //std::cout<<"Bind Port"<<port_<<" error!"<<std::endl;
        close(listenFd_);
        return false;
    }

    // listen() 只是让套接字处于监听状态，并没有接收请求。接收请求需要使用 accept() 函数
    ret = listen(listenFd_, 6);     // 参数 6 请求队列的大小
    if (ret < 0) {
        //printf("Listen port:%d error!\n", port_);
        close(listenFd_);
        return false;
    }
    ret = epoller_->addFd(listenFd_, listenEvent_ | EPOLLIN);
    if (ret == 0) {
        //printf("Add listen error!\n");
        close(listenFd_);
        return false;
    }
    setFdNonblock(listenFd_);   // 设置非阻塞
    //printf("Server port:%d\n", port_);
    return true;
}

int WebServer::setFdNonblock(int fd) {
    assert(fd > 0);
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);
}
