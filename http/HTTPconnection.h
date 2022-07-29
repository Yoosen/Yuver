#ifndef HTTP_CONNECTION_H
#define HTTP_CONNECTION_H

#include<arpa/inet.h> //sockaddr_in
#include<sys/uio.h> //readv/writev
#include<iostream>
#include<sys/types.h>
#include<assert.h>

#include "../buffer/buffer.h"
#include "HTTPrequest.h"
#include "HTTPresponse.h"

class HTTPconnection {
public:
    HTTPconnection();
    ~HTTPconnection();

    // 初始化接口
    void initHTTPConn(int socketFd, const sockaddr_in& addr);

    //每个连接中定义的对缓冲区的读写接口
    ssize_t readBuffer(int* saveErrno);
    ssize_t writeBuffer(int* saveErrno);

    //关闭HTTP连接的接口
    void closeHTTPConn();
    //定义处理该HTTP连接的接口，主要分为request的解析和response的生成
    bool handleHTTPConn();

    //其他方法 后面加 const表示函数不可以修改class的成员
    const char* getIP() const; // 获取 ip
    int getPort() const;        // 获取端口
    int getFd() const;          // 获取HTTP连接的描述符，也就是唯一标志
    sockaddr_in getAddr() const;    // 获取地址

    // 获取已经写入的数据长度
    int writeBytes() {
        return iov_[1].iov_len + iov_[0].iov_len;
    }

    // 获取这个HTTP连接 KeepAlive 的状态
    bool isKeepAlive() const
    {
        return request_.isKeepAlive();
    }

    static bool isET;
    static const char* srcDir;
    static std::atomic<int>userCount;   // 加锁会浪费性能，atomic可以提升性能

private:
    int fd_;                  //HTTP连接对应的描述符
    struct sockaddr_in addr_;
    bool isClose_;            //标记是否关闭连接

    int iovCnt_;
    struct iovec iov_[2];

    Buffer readBuffer_;       //读缓冲区
    Buffer writeBuffer_;      //写缓冲区

    HTTPrequest request_;
    HTTPresponse response_;

};

#endif //HTTP_CONNECTION_H