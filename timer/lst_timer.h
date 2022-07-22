#ifndef LST_TIMER
#define LST_TIMER

#include <time.h>
#include "../log/log.h"

class util_timer;
struct client_data {
    sockaddr_in address;
    int sockfd;
    util_timer *timer;
};

class util_timer {
public:
    time_t expire;
    void (*cb_func)(client_data *);
    client_data *user_data;
    util_timer *prev;
    util_timer *next;

public:
    util_timer() : prev(NULL), next(NULL) {}
};

class sort_timer_lst {
private:
    util_timer *head;
    util_timer *tail;

private:
    void add_timer(util_timer *timer, util_timer *lst_head) {
        util_timer *prev = lst_head;
        util_timer *tmp = prev->next;

        // 从小到达的过期时间双链表，在前面的先过期
        while(tmp) {
            if(timer->expire < tmp->expire) {
                prev->next = timer;
                timer->next = tmp;
                tmp->prev = timer;
                timer->prev = prev;
                break;
            }
            prev = tmp;
            tmp = tmp->next;
        }
        // 如果双链表并没有元素
        if(!tmp) {
            prev->next = timer;
            timer->prev = prev;
            timer->next = NULL;
            tail = timer;
        }
    }

public:
    sort_timer_lst() : head(NULL), tail(NULL) {}
    ~sort_timer_lst() {
        util_timer *tmp = head;
        while(tmp) {
            head = tmp->next;
            delete tmp;
            tmp = head;
        }
    }

    // 添加一个定时器
    void add_timer(util_timer *timer) {
        if(!timer) {
            return ;
        }

        //当前链表为空
        if(!head) {
            head = tail = timer;
            return ;
        }
        //根据绝对超时时间来插入到链表中
        if(time->expire < head->expire) {
            timer->next = head;
            head->prev = timer;
            head = timer;
            return ;
        }
        //其他的情况调用add_timer(timer,head);
        add_timer(timer, head);
    }

    void adjust_timer(util_timer *timer) {
        if(!timer) {
            return ;
        }

        util_timer *tmp = timer->next;
        // 是链表最后一个 或者 顺序是正确的 
        if(!tmp || (timer->expire < tmp->expire)) {
            return ;
        }
        if(timer == head) {
            head = head->next;
            head->prev = NULL;
            timer->next = NULL;
            add_timer(timer, head); //从列表中抽走,然后重新排序
        }
        else {
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            add_timer(timer, timer->next);
        }
    }

    void del_timer(util_timer *timer) {
        if(!timer) {
            return ;
        }
        if((timer == head) && (timer == tail)) {
            delete timer;
            head = NULL;
            tail = NULL;
            return ;
        }
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
    }

    void tick() {
        if(!head) {
            return ;
        }
        LOG_INFO("%s","timer tick");
        Log::get_instance()->flush();
        time_t cur = time(NULL);
        util_timer *tmp = head;

        while(tmp) {
            // 还没过期
            if(cur < tmp->expire) {
                break;
            }
            // 超时 ，执行回调函数
            tmp->cb_func(tmp->user_data);
            head = tmp->next;   //将tmp移除定时器队列

            if(head) {
                head->prev = NULL;
            }
            delete tmp;
            tmp = head;
        }
    }
};

#endif