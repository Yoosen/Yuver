/*
 * @Author       : Yoosen
 * @Date         : 2022-07-28
 */

#ifndef SQLCONNPOOL_H
#define SQLCONNPOOL_H

#include <mysql/mysql.h>
#include <string>   // c++
#include <queue>
#include <mutex> // 互斥
#include <semaphore.h> //信号
#include <thread>

class SqlConnPool {
public:
    static SqlConnPool* Instance(); // 单例模式

    MYSQL* GetConn();   // 获取连接
    void FreeConn(MYSQL* conn); // 释放连接
    int GetFreeConnCount();     // 获取空闲数

    void Init(const char* host, int port, const char* user,
        const char* passwd, const char* dbName, int connSize);

    void ClosePool();

private:
    SqlConnPool();  // 声明为 private 不允许外部通过该类构造 Sql，该类的 static 公有成员可以通过该构造函数进行实例化
    ~SqlConnPool(); // 析构函数私有化的类的设计可以保证只能用new命令在堆中来生成对象，只能动态的去创建对象，这样可以自由的控制对象的生命周期。但是，这样的类需要提供创建和撤销的公共接口
    int MAX_CONN_;
    int useCount_;  // 使用数
    int freeCount_;  // 空闲数

    std::queue<MYSQL*> connQue_;
    std::mutex mtx_;
    sem_t semId_;



};

#endif  // SQLCONNPOOL_H

