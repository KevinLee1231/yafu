/*----------------------------------------------------------------------
MIT License

Copyright (c) 2021 bbuhrow

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
----------------------------------------------------------------------*/

#ifndef _THREADPOOL_H
#define _THREADPOOL_H

#include <stdlib.h>
#include <functional>

//#define USE_TPOOL_AFFINITY

#ifdef USE_TPOOL_AFFINITY
#define __USE_GNU
#include <sched.h>

#endif


#include <pthread.h>
#include <unistd.h>


enum tpool_state {
    TPOOL_STATE_INIT,
    TPOOL_STATE_WAIT,
    TPOOL_STATE_WORK,
    TPOOL_STATE_END
};

// 前置声明：成员里要用 std::copyable_function<void(tpool_t*)>，
// 而结构体自己还没定义完，注入的类名在模板实参里用不了。
struct tpool_t;

struct tpool_t
{
    int tindex;
    int tstartup;
    int num_threads;
    int done;
    int debug;

    volatile enum tpool_state state;

    int num_work_fcn;   // number of work functions
    int work_fcn_id;   // index of work function to execute

    // 用户定义的回调，在线程池的各个状态上执行。
    // 用 C++26 的 std::copyable_function（P2943）而不是"函数指针存成 void*、
    // 调用时再转回来"：后者在标准里只是有条件支持，而且签名写错也查不出来
    // —— ecm_start_fcn / ecm_stop_fcn 实际上是 void(tpool_t*)，一直被声明
    // 成 void(void*) 就是这么来的。现在签名不对直接编不过，带捕获的
    // lambda 也能注册。
    std::copyable_function<void(tpool_t*)> tpool_start_fcn;
    std::copyable_function<void(tpool_t*)> tpool_stop_fcn;
    std::copyable_function<void(void*)> tpool_sync_fcn;
    std::copyable_function<void(void*)> tpool_dispatch_fcn;
    std::copyable_function<void(void*)> tpool_work_fcn[16];

    // a user-defined structure to carry data throughout the threadpool.
    // user will have to cast this to their appropriate type before use.
    void *user_data;

    /* fields for thread pool synchronization */
    volatile int *thread_queue;
    volatile int *threads_waiting;

    pthread_t thread_id;
    pthread_mutex_t run_lock;
    pthread_cond_t run_cond;

    pthread_mutex_t *queue_lock;
    pthread_cond_t *queue_cond;

#ifdef USE_TPOOL_AFFINITY
    pthread_attr_t *attr;
    cpu_set_t *cpus;
#endif


};

void tpool_go(tpool_t *thread_data);
void tpool_add_work_fcn(tpool_t *tdata, std::copyable_function<void(void*)> work_fcn);
tpool_t * tpool_setup(int num_threads,
    std::copyable_function<void(tpool_t*)> start_fcn,
    std::copyable_function<void(tpool_t*)> stop_fcn,
    std::copyable_function<void(void*)> sync_fcn,
    std::copyable_function<void(void*)> dispatch_fcn,
    void *udata);
// 线程池数组是 new[] 出来的（tpool_t 里现在有非平凡成员），释放走这里。
void tpool_free(tpool_t *tdata);
#endif // #ifndef _THREADPOOL_H
