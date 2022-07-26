#ifndef TIMER_H
#define TIMER_H

#include<queue>
#include<deque>
#include<unordered_map>
#include<ctime>
#include<chrono>
#include<functional>
#include<memory>

#include "HTTPconnection.h"

// 为了提高Web服务器的效率，我们考虑给每一个HTTP连接加一个定时器。

// 定时器给每一个HTTP连接设置一个过期时间，然后我们定时清理超过过期时间的连接，会减少服务器的无效资源的耗费，提高服务器的运行效率。

// 我们还需要考虑一下如何管理和组织这些定时器。设置定时器的主要目的是为了清理过期连接，为了方便找到过期连接，首先考虑使用优先队列，按过期时间排序，让过期的排在前面就可以了。但是这样的话，虽然处理过期连接方便了，当时没法更新一个连接的过期时间。

// 最后，选择一个折中的方法。用vector容器存储定时器，然后在这之上实现堆结构，这样各个操作的代价就得到了相对均衡。

typedef std::function<void()> TimeoutCallBack;
typedef std::chrono::high_resolution_clock Clock;
typedef std::chrono::milliseconds MS;
typedef Clock::time_point TimeStamp;
//typedef std::unique_ptr<HTTPconnection> HTTPconnection_Ptr;

class TimerNode{
public:
    int id;             //用来标记定时器
    TimeStamp expire;   //设置过期时间
    TimeoutCallBack cb; //设置一个回调函数用来方便删除定时器时将对应的 HTTP 连接关闭

    //需要的功能可以自己设定
    bool operator<(const TimerNode& t)
    {
        return expire<t.expire;
    }
};

class TimerManager{
    typedef std::shared_ptr<TimerNode> SP_TimerNode;
public:
    TimerManager() {heap_.reserve(64);}
    ~TimerManager() {clear();}
    //设置定时器 
    void addTimer(int id,int timeout,const TimeoutCallBack& cb);    // 添加定时器
    //处理过期的定时器
    void handle_expired_event();
    //下一次处理过期定时器的时间
    int getNextHandle();

    // HTTP连接的处理过程中需要的对某一个连接对应定时器的过期时间做出改变所需要的update方法和处理过期时间过程中需要调用的work方法
    void update(int id,int timeout);
    //删除指定id节点，并且用指针触发处理函数
    void work(int id);

    void pop();
    void clear();

private:
    void del_(size_t i);        // 删除指定计时器
    void siftup_(size_t i);     // 向上调整
    bool siftdown_(size_t index,size_t n);  // 向下调整
    void swapNode_(size_t i,size_t j);  // 交换两个结点位置

    std::vector<TimerNode>heap_;
    std::unordered_map<int,size_t>ref_;//映射一个fd对应的定时器在heap_中的位置
};

#endif //TIMER_H