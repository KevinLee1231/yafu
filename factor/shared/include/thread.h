/*--------------------------------------------------------------------
This source distribution is placed in the public domain by its author,
Jason Papadopoulos. You may use it for any purpose, free of charge,
without having to notify anyone. I disclaim any responsibility for any
errors.

Optionally, please be nice and tell me if you find this source to be
useful. Again optionally, if you add to the functionality present here
please consider making those additions public too, so that others may 
benefit from your work.	

$Id: thread.h 817 2012-11-11 14:58:29Z jasonp_sf $
--------------------------------------------------------------------*/

#ifndef _THREAD_H_
#define _THREAD_H_

#include <util.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* mutexes ---------------------------------------------------------*/

typedef pthread_mutex_t mutex_t;

static INLINE void mutex_init(mutex_t *m)
{
	pthread_mutex_init(m, NULL);
}

static INLINE void mutex_free(mutex_t *m)
{
	pthread_mutex_destroy(m);
}

static INLINE void mutex_lock(mutex_t *m)
{
	pthread_mutex_lock(m);
}

static INLINE void mutex_unlock(mutex_t *m)
{
	pthread_mutex_unlock(m);
}

/* a thread pool --------------------------------------------------*/

typedef void (*init_func)(void *data, int thread_num);
typedef void (*run_func)(void *data, int thread_num);
typedef void (*shutdown_func)(void *data, int thread_num);

typedef struct {
	init_func init;
	shutdown_func shutdown;
	void *data;
} thread_control_t;

typedef struct {
	init_func init;
	run_func run;
	shutdown_func shutdown;
	void *data;
} task_control_t;

struct threadpool* threadpool_init(int num_threads, 
				   int queue_size, 
				   thread_control_t *t);

int threadpool_add_task(struct threadpool *pool, 
			task_control_t *t, 
			int blocking);

void threadpool_free(struct threadpool *pool);

/* returns zero if no pending tasks */
int threadpool_drain(struct threadpool *pool,
			int blocking);


#ifdef __cplusplus
}
#endif
#endif /* !_THREAD_H_ */
