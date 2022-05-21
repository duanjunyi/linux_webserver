#include "epoll.h"

epollCls::epollCls()
{
    m_epollfd = epoll_create(5);
    m_events = new epoll_event[MAX_EVENT_NUMBER];
}

epollCls::~epollCls()
{
    close(m_epollfd);
    delete[] m_events;
}


//向epoll中添加需要监听的文件描述符
void epollCls::addfd(int fd, bool oneshot)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    if(oneshot)
    {
        //防止同一个通信被不同的线程处理
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(m_epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//从epoll中移除监听的文件描述符
void epollCls::removefd(int fd)
{
    epoll_ctl(m_epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 修改文件描述符，重置socket上的EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发
void epollCls::modfd(int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events =  ev | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
    epoll_ctl(m_epollfd, EPOLL_CTL_MOD, fd, &event);
}

//设置为非阻塞
int epollCls::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}