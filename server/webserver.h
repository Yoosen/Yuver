/*** 
 * @Author  : Yoosen
 * @Date    : 2022-07-26
 */
#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <unordered_map>
#include <fcntl.h>       // fcntl()
#include <unistd.h>      // close()
#include <assert.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../server/epoller.h"
#include "../timer/timer.h"
#include "../pool/threadpool.h"
#include "../http/HTTPconnection.h"
#include "../pool/sqlconnpool.h"
#include "../pool/sqlconnRAII.h"

class WebServer {
public:
    WebServer(int port, int trigMode, int timeoutMS, bool optLinger, int threadNum);

    // Yoosen 支持数据库的构造函数
    WebServer(int port, int trigMode, int timeoutMS, bool optLinger,
        int threadNum, int sqlPort, const char* sqlUser,
        const char* sqlPasswd, const char* dbName, int connPoolNum);


    ~WebServer();

    void Start(); //一切的开始

private:
    //对服务端的 socket 进行设置，最后可以得到 listenFd
    bool initSocket_();

    void initEventMode_(int trigMode);

    // 处理 http 连接
    void addClientConnection(int fd, sockaddr_in addr); //添加一个HTTP连接
    void closeConn_(HTTPconnection* client);            //关闭一个HTTP连接


    void handleListen_();
    void handleWrite_(HTTPconnection* client);
    void handleRead_(HTTPconnection* client);

    void onRead_(HTTPconnection* client);
    void onWrite_(HTTPconnection* client);
    void onProcess_(HTTPconnection* client);

    void sendError_(int fd, const char* info);
    void extentTime_(HTTPconnection* client);

    static const int MAX_FD = 65536;
    static int setFdNonblock(int fd);

    int port_;
    int timeoutMS_;  /* 毫秒MS,定时器的默认过期时间 */
    bool isClose_;     // 是否关闭
    int listenFd_;
    bool openLinger_;   // 
    char* srcDir_;//需要获取的路径

    uint32_t listenEvent_;
    uint32_t connectionEvent_;

    std::unique_ptr<TimerManager>timer_;
    std::unique_ptr<ThreadPool> threadpool_;
    std::unique_ptr<Epoller> epoller_;      // 独占指针 epoller
    std::unordered_map<int, HTTPconnection> users_;
};


#endif //WEBSERVER_H