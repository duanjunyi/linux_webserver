#ifndef EPOLL_H
#define EPOLL_H


#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>


// epoll多路复用监听连接与新请求
class epollCls
{
    public:
        epoll_event* m_events;                       //触发事件
        int m_epollfd;                               // epoll对象的文件描述符


        epollCls();
        ~epollCls();
        //向epoll中添加需要监听的文件描述符
        void addfd(int fd, bool oneshot);
        //从epoll中移除监听的文件描述符
        void removefd(int fd);
        // 修改文件描述符，重置socket上的EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发
        void modfd(int fd, int ev);
        inline int wait(){return epoll_wait(m_epollfd, m_events, MAX_EVENT_NUMBER, -1);}
        

    private:
        static const int MAX_EVENT_NUMBER = 10000; // 监听的最大的事件数量


        //设置为非阻塞
        int setnonblocking(int fd);
};


#endif