#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>


#include "locker.h"
#include "threadpool.h"
#include "httpconn.h"

const int MAX_FD = 65536; //单进程最大文件描述符数目为1024，所以这里可能存在冗余
const int MAX_PEND_CONN = 16;
const int MAX_CONCURR_CONN = 8;


//信号捕捉
void addsig(int sig, void(handler)(int))
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}



int main(int argc, char* argv[])
{
    if(argc <= 1)
    {
        printf("usage: %s port_number\n", basename(argv[0]));
        return 1;
    }

    //创建服务器监听listenfd
    int port = atoi(argv[1]);
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    int ret = 0;
    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    int reuse = 1; 
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));  //设置端口复用
    int old_op = fcntl(listenfd, F_GETFL);
    int new_op = old_op | O_NONBLOCK;
    ret = fcntl(listenfd, F_SETFL, new_op);                                 //设置非阻塞
    if(ret == -1)
    {
        perror("listenfd nonblock");
        return -1;
    }
    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    if(ret == -1)
    {
        perror("bind");
        return -1;
    }
    ret = listen(listenfd, MAX_PEND_CONN);
    if(ret == -1)
    {
        perror("listen");
        return -1;
    }


    //生成线程池
    threadpool< httpconn >* pool = NULL;
    try {
        pool = new threadpool<httpconn>;
    } catch( ... ) {
        return 1;
    }



    //创建epoll对象并添加监听文件描述符
    epollCls epollObj;
    epollObj.addfd(listenfd, false);


    //初始化连接任务
    httpconn* users = new httpconn[MAX_FD];
    httpconn::m_epollObj = epollObj;
    httpconn::m_user_count = 0;


    //忽略SIGPIPE信号
    addsig(SIGPIPE, SIG_IGN);


    while(true)
    {
        int number = epollObj.wait();
        if((number < 0) && (errno != EINTR))
        {
            perror("epoll failure\n");
            break;
        }

        for(int i = 0;i < number;i++)
        {
            int sockfd = epollObj.m_events[i].data.fd;


            //新连接
            if(sockfd == listenfd)
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                for(int i = 0;i < MAX_CONCURR_CONN;i++)
                {
                    int connfd = accept(sockfd, (struct sockaddr*)&client_address, &client_addrlen);

                    if(connfd < 0)
                    {
                        //没有新的连接
                        if(errno == EAGAIN)
                            break;
                        //被其它信号打断
                        else if(errno == EINTR)
                            continue;
                        //其它错误
                        else
                        {
                            perror("new connection failed");
                            continue;
                        }
                    }
                    else
                    {
                        //超过最大用户数
                        if(httpconn::m_user_count >= MAX_FD)
                        {

                            close(connfd);
                            printf("new connection closed for the excess of user count\n");
                            continue;
                        }
                        else
                            users[connfd].init(connfd, client_address);
                    }

                }
            }
            //单向连接关闭或者错误
            else if(epollObj.m_events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
                users[sockfd].close_conn();
            //读事件
            else if(epollObj.m_events[i].events & EPOLLIN)
            {
                if(users[sockfd].read())
                    pool->append(users + sockfd);
                else
                    users[sockfd].close_conn();
            }
            //写事件
            else if(epollObj.m_events[i].events & EPOLLOUT)
            {
                if(!users[sockfd].write())
                    users[sockfd].close_conn();
            }
        }
    }
    

    close(listenfd);
    delete[] users;
    delete pool;
    return 0;    
}