#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>


#define MAX_FD 65536            // 最大文件描述符
#define MAX_EVENT_NUMBER 1000   // 最大事件数
#define TIMESLOT 5              // 最小超时单位

