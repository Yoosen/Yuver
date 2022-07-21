#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool() {
    this->CurConn = 0;  // 当前连接数
    this->FreeConn = 0; // 已经连接数
}

// 单例模式
connection_pool *connection_pool::GetInstance() {
    static connection_pool connPool;
    return &connPool;
}

void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, unsigned int MaxConn) {
    this->url = url;
    this->Port = Port;
    this->User = User;
    this->PassWord = PassWord;
    this->DatabaseName = DBName;

    // 防止并发初始化需要加锁处理？
    lock.lock();
    for(int i = 0; i < MaxConn; ++i) {
        MYSQL *con = NULL;
        // 1.初始化连接
        con = mysql_init(con);

        if (con == NULL) {
            cout << "Error:" << mysql_error(con);
            exit(1);
        }

        con = mysql_real_connec(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);
        
        if (con == NULL) {
            cout << "Error:" << mysql_error(con);
            exit(1);
        }
        connList.push_back(con);
        ++FreeConn;
    }

    // 空闲连接的信号量
    reserve = sem(FreeConn);

    // 初始时，当前最大连接就是剩余空闲连接
    this->MaxConn = FreeConn;

    lock.unlock();
}

//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection() {
    MYSQL *con = NULL;

    if(connList.size() == 0)
        reutrn NULL;
    
    // 信号量 P 操作，使用了一个线程，可用资源减 1
    reserve.wait();

    // 互斥锁
    lock.lock();

    con = connList.front();
    connList.pop_front();
    
    --FreeConn;
    ++CurConn;

    lock.unlock();

    return con;
}

//释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con) {
    if (con == NULL)
        return false;

    lock.lock();

    connList.push_back(con);
    ++FreeConn;
    --CurConn;

    lock.unlock();

    // V 操作，释放连接资源，可用连接数加 1
    reserve.post();
    return true;
}

//销毁数据库连接池
void connection_pool::DestroyPool() {
    lock.lock();
    if(connList.size() > 0) {
        list<MYSQL *>::iterator it;

        for(it = connList.begin(); it != connList.end(); ++it) {
            MYSQL *con = *it;
            mysql_close(con);
        }

        CurConn = 0;
        FreeConn = 0;
        connList.clear();

        lock.unlock();
    }

    lock.unlock();
}

// 获取当前空闲的连接数
int connection_pool::GetFreeConn() {
    return this->FreeConn;
}

connection_pool::~connection_pool() {
    DestroyPool();
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool) {
    *SQL = connPool->GetConnection();

    conRAII = *SQL;
    poolRAII = connPool;
}

connectionRAII::~connectionRAII() {
    poolRAII->ReleaseConnection(conRAII);
}