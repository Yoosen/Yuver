#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<thread>
#include<condition_variable>
#include<mutex>
#include<vector>
#include<queue>
#include<future>

class ThreadPool {
private:
    bool m_stop;
    std::vector<std::thread>m_thread;   // 线程池
    std::queue<std::function<void()>>tasks;
    std::mutex m_mutex;     // 互斥锁
    std::condition_variable m_cv;   // 条件变量

public:
    // explicit 禁止隐式转换
    explicit ThreadPool(size_t threadNumber) :m_stop(false) {
        for (size_t i = 0;i < threadNumber;++i)
        {
            m_thread.emplace_back(
                [this]() {
                    for (;;)
                    {
                        std::function<void()>task;
                        {
                            std::unique_lock<std::mutex>lk(m_mutex);
                            m_cv.wait(lk, [this]() { return m_stop || !tasks.empty();});
                            if (m_stop && tasks.empty()) return;
                            task = std::move(tasks.front());
                            tasks.pop();
                        }
                        task();
                    }
                }
            );
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;

    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex>lk(m_mutex);
            m_stop = true;
        }
        m_cv.notify_all();
        for (auto& threads : m_thread)
        {
            threads.join();
        }
    }

    // std::future C++ 11   future对象提供访问异步操作结果的机制，很轻松解决从异步任务中返回结果
    // 会在将来某个时间获得异步的结果
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args)->std::future<decltype(f(args...))> {
        auto taskPtr = std::make_shared<std::packaged_task<decltype(f(args...))()>>(      // decltype 自动推导函数返回值类型
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)   // std::bind() 函数适配器
            );
        // packaged_task将普通的可调用函数对象转换为异步执行的任务
        // std::forward 原封不动的传给下一个函数
        // auto taskPtr = make_shared<packaged_task<f>>()  (bind(forward(f), forward(args)))     // make_shared 共享指针
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            if (m_stop) throw std::runtime_error("submit on stopped ThreadPool");
            tasks.emplace([taskPtr]() { (*taskPtr)(); });
        }
        m_cv.notify_one();
        return taskPtr->get_future();

    }
};

#endif //THREADPOOL_H
