#ifndef _C_COROUTINE_H_
#define _C_COROUTINE_H_

#define COROUTINE_DEAD 0
#define COROUTINE_READY 1
#define COROUTINE_RUNNING 2
#define COROUTINE_SUSPEND 3

struct schedule;

struct schedule *coroutine_open();
void coroutine_close(struct schedule *);

typedef void (*coroutine_func)(struct schedule *, void*);

int coroutine_new(struct schedule *, coroutine_func, void*);
void coroutine_resume(struct schedule *, int);
int coroutine_status(struct schedule *, int);
int coroutine_running(struct schedule *);
void coroutine_yield(struct schedule *);
#endif