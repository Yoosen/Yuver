#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

class sem {
public:
    sem() {
        // sem_init(sem_t *sem, int pshared, unsigned int value)
        // sem 信号量地址
        // pshared 等于 0，信号量在线程共享，不等于0，信号在进程间共享
        // value 信号量的初始值
        // 成功返回0， 失败返回1
        if (sem_init(&m_sem, 0, 0) != 0) {
            throw std::exception();
        }
    }
    sem(int num) {
        if (sem_init(&m_sem, 0, num) != 0) {
            throw std::exception();
        }
    }
    ~sem() {
        sem_destroy(&m_sem);
    }
    bool wait() {
        // sem_wait P操作， sem为信号量地址
        // 成功为0，失败为1
        return sem_wait(&m_sem) == 0;
    }
    bool post() {
        // sem_post V操作
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};

class locker {
private:
    // 互斥量
    pthread_mutex_t m_mutex;

public:
    locker() {
        if (pthread_mutex_init(&m_mutex, NULL) != 0) {
            throw std::exception();
        }
        
    }
    ~locker() {
        pthread_mutex_destroy(&m_mutex);
    }

    bool lock() {
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    bool unlock() {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

    pthread_mutex_t *get() {
        return &m_mutex;
    }
};

class cond {
private:
    pthread_cond_t m_cond;  // 条件变量

public:
    cond() {
        if (pthread_cond_init(&m_cond, NULL) != 0) {
            throw std::exception();
        }
    }
    ~cond() {
        pthread_cond_destroy(&m_cond);
    }

    bool wait(pthread_mutex_t *m_mutex) {
        int ret = 0;
        ret = pthread_cond_wait(&m_cond, m_mutex);
        return ret == 0;
    }

    bool timewait(pthread_mutex_t *m_mutex, struct timespec t) {
        int ret = 0;
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        return ret == 0;
    }

    bool signal() {
        // pthread_cond_signal 唤醒阻塞在条件变量上的线程，唤醒因为条件而阻塞的线程
        return pthread_cond_signal(&m_cond) == 0;
    }

    bool boardcast() {
        return pthread_cond_broadcast(&m_cond) == 0;
    }
};

