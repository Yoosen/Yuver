/*
 * @Author       : Yoosen
 * @Date         : 2022-07-28
 */

#include "sqlconnpool.h"
using namespace std;

SqlConnPool::SqlConnPool() {
    useCount_ = 0;
    freeCount_ = 0;
}

// 单例模式
SqlConnPool* SqlConnPool::Instance() {
    static SqlConnPool connPool;
    return &connPool;
}

void SqlConnPool::Init(const char* host, int port,
    const char* user, const char* passwd,
    const char* dbName, int connSize = 10) {
    
    assert(connSize > 0);
    for(int i = 0; i < connSize; ++i) {
        MYSQL* sql = nullptr;
        sql = mysql_init(sql);

        if(!sql) {
            // Mysql init error
            assert(sql);
        }
        sql = mysql_real_connect(sql, port, user, passwd, dbName, port, nullptr, 0);

        if(!sql) {
            // Mysql connect error
        }
        connQue_.push(sql);
    }

    MAX_CONN_ = connSize;

    // sem_init()
    // 简述：创建信号量
    // 第一个参数：指向的信号对象
    // 第二个参数：控制信号量的类型，如果其值为0，就表示信号量是当前进程的局部信号量，否则信号量就可以在多个进程间共享
    // 第三个参数：信号量sem的初始值
    sem_init(&semId_, 0, MAX_CONN_);    // 信号量初值 10
}

// 获取一个连接
MYSQL* SqlConnPool::GetConn() {
    MYSQL* sql = nullptr;
    
    // sql 连接池为空
    if(connQue_.empty()) {
        // SqlConnPool busy
        return nullptr;
    }

    // P 操作，信号量初值 10
    sem_wait(&semId_);
    {
        lock_guard<mutex> locker<mtx_>;
        sql = connQue_.front();
        connQue_.pop();
    }
    return sql;
}

// 释放一个连接
void SqlConnPool::FreeConn(MYSQL* mysql) {
    assert(sql);
    lock_guard<mutex> locker(mtx_);
    connQue_.push(sql);

    // V操作
    sem_post(&semId_);
}

// 关闭连接池
void SqlConnPool::ClosePool() {
    lock_guard<mutex> locker(mtx_);
    // 关闭所有连接
    while(!connQue_.empty()) {
        auto item = connQue_.front();
        connQue_.pop();
        mysql_close(item);
    }
    mysql_library_end();
}

SqlConnPool::~SqlConnPool() {
    ClosePool();
}