#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>


#include "locker.h"

// 线程池类，将它定义为模板类是为了代码复用，模板参数T是任务类
template<typename T>
class threadpool
{
    public:
        /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
        threadpool(int thread_number = 8, int max_requests = 10000);
        ~threadpool();
        bool append(T* request);

    private:
        /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
        static void* worker(void* arg);
        void run();

    private:
        // 线程的数量
        int m_thread_number;

        // 描述线程池的数组，大小为m_thread_number
        pthread_t* m_threads;

        // 请求队列中最多允许的、等待处理的请求的数量
        int m_max_requests;

        // 请求队列
        std::list< T* > m_workqueue;

        // 保护请求队列的互斥锁
        locker m_queuelocker;

        // 是否有任务需要处理
        sem m_queuestat;

        //是否结束线程
        bool m_stop;
};

#endif

