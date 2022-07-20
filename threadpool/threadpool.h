#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template <typename T>
class threadpool {
private:
    static void *worker(void *arg);
    void run();

private:
    int m_thread_number;        // 线程池中的线程数
    int m_max_requests;         // 请求队列中中允许的最大请求数
    pthread_t *m_threads;       // 描述线程池的数组，其大小为 m_thread_number
    std::list<T *> m_workqueue; // 请求队列
    locker m_queuelocker;       // 保护请求队列的互斥锁
    sem m_queuestat;            // 是否有任务需要处理
    bool m_stop;                // 是否结束线程
    connection_pool *m_connPool;    // 数据库

public:
    // thread_number 是线程池中线程的数量
    // max_requests是请求队列中最多允许的、等待处理的请求的数量
    threadpool(connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request);
};

// 线程池的创建
template <typename T>
threadpool<T>::threadpool(connection_pool *connPool, int thread_number, int max_requests) : m_thread_number(thread_number), m_max_requests(max_requests), m_stop(false), m_threads(NULL), m_connPool(connPool) {
    
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    
    //pthread_t是长整型
    // 线程池的数组
    m_threads = new pthread_t[m_thread_number];

    // 创建失败
    if(!m_threads)
        throw std::exception();

    for(int i = 0; i < thread_number; ++i) {
        //创建成功应该返回0，如果线程池在线程创建阶段就失败，那就应该关闭线程池了
        // 传递一个 pthread_t 类型的指针变量
        // worker 以函数指针的方式指明新建线程需要执行的函数
        // this 传给 worker 的参数
        if (pthread_create(m_threads + i, NULL, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }

        // 主要是将线程属性更改为unjoinable，便于资源的释放
        if (pthread_detach(m_threads[i])) {
            delete[] m_threads;
            throw std::exception();
        }
        
    }
}

template <typename T>
threadpool<T>::~threadpool() {
    delete[] m_threads;
    m_stop = true;
}

// todo...
