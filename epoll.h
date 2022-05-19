#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>


// epoll多路复用监听连接与新请求
class epollCls
{
    public:
        epoll_event* m_events;                       //触发事件
        int m_epollfd;                               // epoll对象的文件描述符

        epollCls()
        {
            m_epollfd = epoll_create(5);
            m_events = new epoll_event[MAX_EVENT_NUMBER];
        }

        ~epollCls()
        {
            close(m_epollfd);
            delete[] m_events;
        }


        //向epoll中添加需要监听的文件描述符
        void addfd(int fd, bool oneshot)
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
        void removefd(int fd)
        {
            epoll_ctl(m_epollfd, EPOLL_CTL_DEL, fd, 0);
            close(fd);
        }


        // 修改文件描述符，重置socket上的EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发
        void modfd(int fd, int ev)
        {
            epoll_event event;
            event.data.fd = fd;
            event.events =  ev | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
            epoll_ctl(m_epollfd, EPOLL_CTL_MOD, fd, &event);
        }


        inline int wait()
        {
            return epoll_wait(m_epollfd, m_events, MAX_EVENT_NUMBER, -1);
        }
        

    private:
        static const int MAX_EVENT_NUMBER = 10000; // 监听的最大的事件数量


        //设置为非阻塞
        int setnonblocking(int fd)
        {
            int old_option = fcntl(fd, F_GETFL);
            int new_option = old_option | O_NONBLOCK;
            fcntl(fd, F_SETFL, new_option);
            return old_option;
        }
};