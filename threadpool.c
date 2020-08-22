#include "threadpool.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

/*======= declaration of private function =======*/

void freeAllMemory_DestroyMutexAndCond(threadpool *destroyme);
void freeThreadpoolMemory(threadpool *tp);

/*======= create all the main function =======*/

// create the threadpool, initialize all the value in the threadpool
// return NULL if the threadpool create has failed, else return the threadpool
threadpool *create_threadpool(int num_threads_in_pool)
{
    if (num_threads_in_pool < 1 || num_threads_in_pool > MAXT_IN_POOL)
    { // check if the value of num_threads_in_pool id valid
        return NULL;
    }
    threadpool *tp = (threadpool *)malloc(sizeof(threadpool));
    if (tp == NULL)
    { // tp allocation failed
        return NULL;
    }
    tp->num_threads = 0;
    tp->qsize = 0;
    tp->threads = (pthread_t *)malloc(sizeof(pthread_t) * num_threads_in_pool);
    if (tp->threads == NULL)
    { // tp->threads allocation failed
        free(tp);
        return NULL;
    }
    tp->qhead = NULL;
    tp->qtail = NULL;

    if (pthread_mutex_init(&tp->qlock, NULL) != 0)
    { // tp->qlock mutex_init failed
        freeThreadpoolMemory(tp);
        return NULL;
    }
    if (pthread_cond_init(&tp->q_not_empty, NULL) != 0)
    { // tp->q_not_empty cond_init failed
        pthread_mutex_destroy(&tp->qlock);
        freeThreadpoolMemory(tp);
        return NULL;
    }
    if (pthread_cond_init(&tp->q_empty, NULL) != 0)
    { // tp->q_empty cond_init failed
        pthread_mutex_destroy(&tp->qlock);
        pthread_cond_destroy(&tp->q_not_empty);
        freeThreadpoolMemory(tp);
        return NULL;
    }
    tp->shutdown = 0;
    tp->dont_accept = 0;

    for (int i = 0; i < num_threads_in_pool; i++)
    { // create all the thread and send then to the function do_work
        if (pthread_create(&tp->threads[i], NULL, do_work, (void *)tp) != 0)
        { // check if there is thread that then it is continue with the thread that already created
            break;
        }
        tp->num_threads++;
    }
    if (tp->num_threads != num_threads_in_pool)
    { // if there was fails in the create then change the size of the array to the number of thread that was created
        tp->threads = (pthread_t *)realloc(tp->threads, sizeof(pthread_t) * tp->num_threads);
        if (tp->threads == NULL)
        {
            free(tp);
            return NULL;
        }
    }
    return tp;
}

void dispatch(threadpool *from_me, dispatch_fn dispatch_to_here, void *arg)
{
    if (from_me == NULL || dispatch_to_here == NULL)
    { // the data that sent were invalid
        return;
    }
    // add work to the queue - critical section
    pthread_mutex_lock(&from_me->qlock);
    if (from_me->dont_accept == 1)
    {
        pthread_mutex_unlock(&from_me->qlock);
        return;
    }
    // create new work
    work_t *newWork = (work_t *)malloc(sizeof(work_t));
    if (newWork == NULL)
    { // newWork has failed
        pthread_mutex_unlock(&from_me->qlock);
        return;
    } // initialize the new work
    newWork->arg = arg;
    newWork->next = NULL;
    newWork->routine = dispatch_to_here;

    if (from_me->qsize == 0)
    { // threre is no works in the queue
        from_me->qhead = newWork;
        from_me->qtail = from_me->qhead;
    }
    else
    { // there is at least one work in the queue
        from_me->qtail->next = newWork;
        from_me->qtail = from_me->qtail->next;
        from_me->qtail->next = NULL;
    }
    from_me->qsize++; // adding 1 to the queue size
    // send signal there is a work in the queue
    pthread_cond_signal(&from_me->q_not_empty);
    pthread_mutex_unlock(&from_me->qlock);
}

void *do_work(void *p)
{
    if (p == NULL)
    { // the data that sent were invalid
        return NULL;
    }
    threadpool *tp = (threadpool *)p;
    while (1)
    {
        pthread_mutex_lock(&tp->qlock);
        if (tp->shutdown == 1)
        { // check if the shutdown has started
            pthread_mutex_unlock(&tp->qlock);
            return NULL;
        }
        if (tp->qsize == 0)
        { // check if the queue is empty
            pthread_cond_wait(&tp->q_not_empty, &tp->qlock);
        }
        if (tp->shutdown == 1)
        { // check if the shutdown has started
            pthread_mutex_unlock(&tp->qlock);
            return NULL;
        }
        // take out work from the queue
        work_t *work = tp->qhead;
        if (work == NULL)
        {
            pthread_mutex_unlock(&tp->qlock);
            continue;
        }
        tp->qsize--;
        if (tp->qsize == 0)
        { // check if the queue is empty them head and tail are NULL
            tp->qhead = NULL;
            tp->qtail = NULL;
            if (tp->dont_accept == 1)
            { // check if the shutdown has started
                pthread_cond_broadcast(&tp->q_empty);
            }
        }
        else
        { // if there is at least one work left then moving forward the head
            tp->qhead = tp->qhead->next;
        }
        pthread_mutex_unlock(&tp->qlock);
        if (work != NULL)
        { // send to the the work function
            work->routine(work->arg);
            free(work);
        }
        pthread_mutex_lock(&tp->qlock);

        if (tp->qsize == 0)
        { // check if the queue is empty
            pthread_cond_signal(&tp->q_empty);
        }
        pthread_mutex_unlock(&tp->qlock);
    }
}

// destroy the threadpool, causing all threads in it to die, and then frees all allocate memory with the threadpool
void destroy_threadpool(threadpool *destroyme)
{
    if (destroyme == NULL)
    { // if the threadpool does not exist do nothing
        return;
    }
    // lock the mutex and dont accept new works
    pthread_mutex_lock(&destroyme->qlock);
    destroyme->dont_accept = 1;
    // wait until all the works that active to finish
    if (destroyme->qsize > 0)
    {
        pthread_cond_wait(&destroyme->q_empty, &destroyme->qlock);
    }

    destroyme->shutdown = 1; // strat the distruction of the threads
    pthread_cond_broadcast(&destroyme->q_not_empty);
    pthread_mutex_unlock(&destroyme->qlock); // unlock the mutex

    for (int i = 0; i < destroyme->num_threads; i++)
    { // wait for all the thread to finish
        if (pthread_join(destroyme->threads[i], NULL) != 0)
        {
            freeAllMemory_DestroyMutexAndCond(destroyme);
            perror("Error, pthread_join has failed");
            exit(EXIT_FAILURE); // join has failed
        }
    }
    freeAllMemory_DestroyMutexAndCond(destroyme); // free memory and close mutex and conditional variables
}

/*======= private function =======*/

// free all the memory allocate and close the mutex and conditional variables
void freeAllMemory_DestroyMutexAndCond(threadpool *destroyme)
{
    pthread_mutex_destroy(&destroyme->qlock);
    pthread_cond_destroy(&destroyme->q_not_empty);
    pthread_cond_destroy(&destroyme->q_empty);
    freeThreadpoolMemory(destroyme); // free all allocate mempry
}

// free memory allocation of the threadpool and the threads
void freeThreadpoolMemory(threadpool *tp)
{
    free(tp->threads);
    free(tp);
}