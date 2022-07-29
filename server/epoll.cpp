/*** 
 * @Author  : Yoosen
 * @Date    : 2022-07-26
 */
#include "epoller.h"

Epoller::Epoller(int maxEvent) :epollerFd_(epoll_create(512)), events_(maxEvent) {
    assert(epollerFd_ >= 0 && events_.size() > 0);
}

Epoller::~Epoller() {
    close(epollerFd_);
}

// 将描述符fd加入到epoll监控
bool Epoller::addFd(int fd, uint32_t events) {
    if (fd < 0) return false;
    epoll_event ev = { 0 };
    ev.data.fd = fd;
    ev.events = events;
    return 0 == epoll_ctl(epollerFd_, EPOLL_CTL_ADD, fd, &ev);
}

// 修改描述符fd对应的事件
bool Epoller::modFd(int fd, uint32_t events) {
    if (fd < 0) return false;
    epoll_event ev = { 0 };
    ev.data.fd = fd;
    ev.events = events;
    return 0 == epoll_ctl(epollerFd_, EPOLL_CTL_MOD, fd, &ev);
}

// 将描述fd移除epoll的监控
bool Epoller::delFd(int fd) {
    if (fd < 0) return false;
    epoll_event ev = { 0 };
    return 0 == epoll_ctl(epollerFd_, EPOLL_CTL_DEL, fd, &ev);
}

// 部分功能已经由epoll_wait方法实现了，也只是需要在这基础上封装一下就可以了
int Epoller::wait(int timeoutMs) {  // 在没有检测到事件发生时最多等待的时间
    return epoll_wait(epollerFd_, &events_[0], static_cast<int>(events_.size()), timeoutMs);
}

//获取fd的函数，size_t 无符号，为了移植
int Epoller::getEventFd(size_t i) const {
    assert(i < events_.size() && i >= 0);
    return events_[i].data.fd;
}

//获取events的函数， 后面的const表示不能修改该类的成员，前面const表示返回值为const
uint32_t Epoller::getEvents(size_t i) const {
    assert(i < events_.size() && i >= 0);
    return events_[i].events;
}