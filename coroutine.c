#include "coroutine.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define DEFAULT_CAP 16
#define DEFAULT_THREAD 16

struct thread_pool *P;

struct thread_pool *pool_open();
struct thread_task *pool_task(struct thread_pool *P);
void pool_push(struct thread_pool *P, struct thread_task *task);
void *thread_func(void *ud);

typedef void *(*task_func)(void*);
struct thread_task {
	task_func func;
	void *ud;
};

struct thread_pool {
	struct thread_task **tasks;
	int ntask;
	int cap;
	pthread_mutex_t *mutex;
	pthread_cond_t *cond;
	pthread_t tid[DEFAULT_THREAD];
};

struct thread_pool *pool_open() {
	struct thread_pool *P = malloc(sizeof(*P));
	memset(P, 0, sizeof(*P));
	P->ntask = 0;
	P->cap = DEFAULT_CAP;
	P->tasks = malloc(sizeof(struct thread_task*) * P->cap);
	memset(P->tasks, 0, sizeof(struct thread_task*) * P->cap);
	P->mutex = malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(P->mutex, NULL);
	P->cond = malloc(sizeof(pthread_cond_t));
	pthread_cond_init(P->cond, NULL);
	int i;
	for (i = 0; i < DEFAULT_THREAD; i++) {
		pthread_create(&P->tid[i], NULL, thread_func, P);
	}
	return P;
}

struct thread_task *pool_task(struct thread_pool *P) {
	struct thread_task *task = NULL;
	while (1) {
		pthread_mutex_lock(P->mutex);
		if (P->ntask == 0) {
			pthread_cond_wait(P->cond, P->mutex);
		} else {
			int i;
			for (i = 0; i < P->cap; i++) {
				if (P->tasks[i]) {
					task = P->tasks[i];
					P->tasks[i] = NULL;
					P->ntask--;
					break;
				}
			}
		}
		pthread_mutex_unlock(P->mutex);
		if (task) break;
	}
	return task;
}

void pool_push(struct thread_pool *P, struct thread_task *task) {
	pthread_mutex_lock(P->mutex);
	if (P->ntask >= P->cap) {
		P->tasks = realloc(P->tasks, sizeof(struct thread_task*) * P->cap * 2);
		memset(P->tasks + P->cap, 0, sizeof(struct thread_task*) * P->cap);
		P->tasks[P->cap] = task;
		P->cap *= 2;
		P->ntask++;
		pthread_cond_signal(P->cond);
	} else {
		int i;
		for (i = 0; i < P->cap; i++) {
			if (!P->tasks[i]) {
				P->tasks[i] = task;
				P->ntask++;
				pthread_cond_signal(P->cond);
				break;
			}
		}
	}
	pthread_mutex_unlock(P->mutex);
}

void *thread_func(void *ud) {
	struct thread_pool *P = ud;
	while (1) {
		struct thread_task *task = pool_task(P);
		task->func(task->ud);
		free(task);
		task = NULL;
	}
}

struct schedule {
	int nco;
	int cap;
	struct coroutine **co;
	int running;

	pthread_mutex_t *mutex_wait;
	pthread_cond_t *cond_wait;
};

struct coroutine{
	coroutine_func main_func;
	void *ud;
	int status;

	pthread_mutex_t *mutex;
	pthread_cond_t *cond;
};

void init_pool() {
	P = pool_open();
}

struct schedule *coroutine_open() {
	pthread_once_t once = PTHREAD_ONCE_INIT;
	pthread_once(&once, init_pool);

	struct schedule *S = malloc(sizeof(*S));
	S->nco = 0;
	S->cap = DEFAULT_CAP;
	S->running = -1;
	S->co = malloc(sizeof(struct coroutine *) * S->cap);
	memset(S->co, 0, sizeof(struct coroutine *) * S->cap);
	S->mutex_wait = malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(S->mutex_wait, NULL);
	S->cond_wait = malloc(sizeof(pthread_cond_t));
	pthread_cond_init(S->cond_wait, NULL);
	return S;
}

void _co_delete(struct coroutine *co) {
	pthread_mutex_destroy(co->mutex);
	pthread_cond_destroy(co->cond);
	free(co->mutex);
	free(co->cond);
	free(co);
}

void coroutine_close(struct schedule *S) {
	int i;
	for (i = 0; i < S->cap; i++){
		struct coroutine *co = S->co[i];
		if (co) {
			_co_delete(co);
		}
	}
	pthread_mutex_destroy(S->mutex_wait);
	pthread_cond_destroy(S->cond_wait);
	free(S->mutex_wait);
	free(S->cond_wait);
	free(S->co);
	S->co = NULL;
	free(S);
}

int coroutine_new(struct schedule *S, coroutine_func func, void *ud) {
	struct coroutine *co = malloc(sizeof(*co));
	co->main_func = func;
	co->ud = ud;
	co->status = COROUTINE_READY;
	co->mutex = malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(co->mutex, NULL);
	co->cond = malloc(sizeof(pthread_cond_t));
	pthread_cond_init(co->cond, NULL);

	if (S->nco >= S->cap) {
		int id = S->cap;
		S->co = realloc(S->co, 2 * S->cap * sizeof(struct coroutine*));
		memset(S->co + S->cap, 0, S->cap);
		S->cap *= 2;
		++S->nco;

		S->co[id] = co;
		return id;
	} else {
		int i;
		for (i = 0; i < S->cap; i++) {
			if (S->co[i] == NULL) {
				S->co[i] = co;
				++S->nco;
				break;
			}
		}
		return i;
	}
}

void* mainfunc(void *ud) {
	struct schedule *S = ud;
	int id = S->running;
	struct coroutine *co = S->co[id];
	co->main_func(S, co->ud);
	_co_delete(co);
	S->co[id] = NULL;
	S->nco--;
	S->running = -1;
	pthread_mutex_lock(S->mutex_wait);
	pthread_cond_signal(S->cond_wait);
	pthread_mutex_unlock(S->mutex_wait);
	return NULL;
}

void coroutine_resume(struct schedule *S, int id) {
	assert(S->running == -1);
	assert(id >= 0 && id < S->cap);

	struct coroutine *co = S->co[id];
	if (NULL == co) {
		return;
	}

	switch(co->status) {
	case COROUTINE_READY:
	{
		co->status = COROUTINE_RUNNING;
		S->running = id;
		
		struct thread_task *task = malloc(sizeof(*task));
		task->func = mainfunc;
		task->ud = S;
		
		pthread_mutex_lock(S->mutex_wait);
		pool_push(P, task);
		pthread_cond_wait(S->cond_wait, S->mutex_wait);
		pthread_mutex_unlock(S->mutex_wait);
	}break;
	case COROUTINE_SUSPEND:
	{
		co->status = COROUTINE_RUNNING;
		S->running = id;
		pthread_mutex_lock(S->mutex_wait);

		pthread_mutex_lock(co->mutex);
		pthread_cond_signal(co->cond);
		pthread_mutex_unlock(co->mutex);

		pthread_cond_wait(S->cond_wait, S->mutex_wait);
		pthread_mutex_unlock(S->mutex_wait);
	}break;
	}
}

int coroutine_status(struct schedule *S, int id) {
	assert(id >= 0 && id < S->cap);
	if (S->co[id] == NULL) {
		return COROUTINE_DEAD;
	}

	return S->co[id]->status;
}

int coroutine_running(struct schedule *S) {
	return S->running;
}

void coroutine_yield(struct schedule *S) {
	int id = S->running;
	if (id == -1) return;

	struct coroutine *co = S->co[id];
	co->status = COROUTINE_SUSPEND;
	S->running = -1;

	pthread_mutex_lock(co->mutex);
	
	pthread_mutex_lock(S->mutex_wait);
	pthread_cond_signal(S->cond_wait);
	pthread_mutex_unlock(S->mutex_wait);

	pthread_cond_wait(co->cond, co->mutex);
	pthread_mutex_unlock(co->mutex);
}