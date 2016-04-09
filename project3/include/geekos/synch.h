/*
 * Synchronization primitives
 * Copyright (c) 2001, David H. Hovemeyer <daveho@cs.umd.edu>
 * $Revision: 1.13 $
 * 
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "COPYING".
 */

#ifndef GEEKOS_SYNCH_H
#define GEEKOS_SYNCH_H

#include <geekos/kthread.h>

/*
 * mutex states
 */
enum { MUTEX_UNLOCKED, MUTEX_LOCKED };

struct Mutex {
    int state;
    struct Kernel_Thread* owner;
    struct Thread_Queue waitQueue;
};

#define MUTEX_INITIALIZER { MUTEX_UNLOCKED, 0, THREAD_QUEUE_INITIALIZER }

struct Condition {
    struct Thread_Queue waitQueue;
};

void Mutex_Init(struct Mutex* mutex);
void Mutex_Lock(struct Mutex* mutex);
void Mutex_Unlock(struct Mutex* mutex);

void Cond_Init(struct Condition* cond);
void Cond_Wait(struct Condition* cond, struct Mutex* mutex);
void Cond_Signal(struct Condition* cond);
void Cond_Broadcast(struct Condition* cond);

struct Semaphore;
DEFINE_LIST(Semaphore_List, Semaphore);

struct Semaphore{
	int semaphoreID;
	char * semaphoreName;
	int value;
	int registeredThreadCount;
	struct Kernel_Thread * registeredThreads[60];
	struct Thread_Queue waitingThreads;
	DEFINE_LINK(Semaphore_List, Semaphore);
};
IMPLEMENT_LIST(Semaphore_List, Semaphore);
static __inline__ void Enqueue_Semaphore(struct Semaphore_List *list, struct Semaphore *sem){
	Add_To_Back_Of_Semaphore_List(list, sem);
}
static _inline_ void Remove_Semaphore(struct Semaphore_List *list, struct Semaphore *sem){
	Remove_From_Semaphore_List(list, sem);
}
#define IS_HELD(mutex) \
    ((mutex)->state == MUTEX_LOCKED && (mutex)->owner == g_currentThread)

#endif  /* GEEKOS_SYNCH_H */
