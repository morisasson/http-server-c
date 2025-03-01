#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include "threadpool.h"

// Worker function declaration
void *do_work(void *pool);

// Create the thread pool
threadpool* create_threadpool(int num_threads_in_pool, int max_queue_size) {
    if (num_threads_in_pool <= 0 || max_queue_size <= 0 || num_threads_in_pool > MAXT_IN_POOL || max_queue_size > MAXW_IN_QUEUE)  {
        fprintf(stderr, "Usage: server <port> <pool-size> <queue-size> <max-requests>\n");
        return NULL;
    }

    threadpool *pool = (threadpool *)malloc(sizeof(threadpool));
    if (!pool) {
        perror("Failed to allocate memory for thread pool");
        return NULL;
    }

    pool->num_threads = num_threads_in_pool;
    pool->max_qsize = max_queue_size;
    pool->qsize = 0;
    pool->qhead = NULL;
    pool->qtail = NULL;
    pool->shutdown = 0;
    pool->dont_accept = 0;

    // Initialize mutex and condition variables
    pthread_mutex_init(&(pool->qlock), NULL);
    pthread_cond_init(&(pool->q_not_empty), NULL);
    pthread_cond_init(&(pool->q_empty), NULL);
    pthread_cond_init(&(pool->q_not_full), NULL);

    // Allocate memory for threads
    pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * num_threads_in_pool);
    if (!pool->threads) {
        perror("Failed to allocate memory for threads");
        free(pool);
        return NULL;
    }

    // Create worker threads
    for (int i = 0; i < num_threads_in_pool; i++) {
        if (pthread_create(&(pool->threads[i]), NULL, do_work, pool) != 0) {
            perror("Failed to create thread");
            destroy_threadpool(pool);
            return NULL;
        }
    }

    return pool;
}

// Add a task to the queue
void dispatch(threadpool *from_me, dispatch_fn dispatch_to_here, void *arg) {
    if (from_me == NULL || dispatch_to_here == NULL) return;

    pthread_mutex_lock(&(from_me->qlock));

    if (from_me->dont_accept || from_me->shutdown) {
        pthread_mutex_unlock(&(from_me->qlock));
        return;
    }

    // If the queue is full, wait
    while (from_me->qsize >= from_me->max_qsize) {
        pthread_cond_wait(&(from_me->q_not_full), &(from_me->qlock));
    }

    // Create new task
    work_t *new_task = (work_t *)malloc(sizeof(work_t));
    if (!new_task) {
        perror("Failed to allocate memory for new task");
        pthread_mutex_unlock(&(from_me->qlock));
        return;
    }

    new_task->routine = dispatch_to_here;
    new_task->arg = arg;
    new_task->next = NULL;

    // Add task to queue
    if (from_me->qsize == 0) {
        from_me->qhead = new_task;
        from_me->qtail = new_task;
        pthread_cond_signal(&(from_me->q_not_empty));
    } else {
        from_me->qtail->next = new_task;
        from_me->qtail = new_task;
    }

    from_me->qsize++;

    pthread_mutex_unlock(&(from_me->qlock));
}

// Worker thread function
void *do_work(void *p) {
    threadpool *pool = (threadpool *)p;

    while (1) {
        pthread_mutex_lock(&(pool->qlock));

        while (pool->qsize == 0 && !pool->shutdown) {
            pthread_cond_wait(&(pool->q_not_empty), &(pool->qlock));
        }

        if (pool->shutdown) {
            pthread_mutex_unlock(&(pool->qlock));
            pthread_exit(NULL);
        }

        // Get task from queue
        work_t *task = pool->qhead;
        if (task) {
            pool->qhead = task->next;
            pool->qsize--;

            if (pool->qsize == 0) {
                pool->qtail = NULL;
                pthread_cond_signal(&(pool->q_empty));
            }

            pthread_cond_signal(&(pool->q_not_full));
        }

        pthread_mutex_unlock(&(pool->qlock));

        if (task) {
            task->routine(task->arg);
            free(task);
        }
    }

    return NULL;
}

// Destroy the thread pool
void destroy_threadpool(threadpool *destroyme) {
    if (destroyme == NULL) return;

    pthread_mutex_lock(&(destroyme->qlock));

    destroyme->dont_accept = 1;

    while (destroyme->qsize != 0) {
        pthread_cond_wait(&(destroyme->q_empty), &(destroyme->qlock));
    }

    destroyme->shutdown = 1;

    pthread_cond_broadcast(&(destroyme->q_not_empty));
    pthread_mutex_unlock(&(destroyme->qlock));

    // Join all threads
    for (int i = 0; i < destroyme->num_threads; i++) {
        pthread_join(destroyme->threads[i], NULL);
    }

    // Free resources
    free(destroyme->threads);

    pthread_mutex_destroy(&(destroyme->qlock));
    pthread_cond_destroy(&(destroyme->q_not_empty));
    pthread_cond_destroy(&(destroyme->q_empty));
    pthread_cond_destroy(&(destroyme->q_not_full));

    free(destroyme);
}
