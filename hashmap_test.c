#define _GNU_SOURCE
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <sys/syscall.h>      /* Definition of SYS_* constants */

#include "uthash.h"

typedef struct detail_s
{
    uint64_t key;
    uint64_t value;
    UT_hash_handle hh;
} detail_t;

typedef struct summary_s
{
    int64_t tid;
    detail_t* detail;
    UT_hash_handle hh;
} summary_t;

// #define MEM_TRACER_MAX_THREAD_NUM 1024*1024
// detail_t* summary[MEM_TRACER_MAX_THREAD_NUM] = {0};

// atomic_int detail_idx = 0;
// __thread int th_local_idx = -1;

summary_t* summary = NULL;
pthread_mutex_t summary_mutex = PTHREAD_MUTEX_INITIALIZER;
__thread summary_t* td_local_summary = NULL;
__thread int64_t tid = -1;

pid_t gettid()
{
  return syscall(SYS_gettid);
}

void* calc(void* argc)
{
    if (tid == -1)
    {
        tid = (int64_t)gettid();
    }

    if (td_local_summary == NULL)
    {
        pthread_mutex_lock(&summary_mutex);

        summary_t* tmp = NULL;
        HASH_FIND_INT(summary, &tid, tmp); /* id already in the hash? */
        if (tmp == NULL)
        {
            tmp = (summary_t*)calloc(1, sizeof(summary_t));
            tmp->tid = tid;
            HASH_ADD_INT(summary, tid, tmp);
        }
        td_local_summary = tmp;
        pthread_mutex_unlock(&summary_mutex);
    }

    int key = 0;
    for (key = 0; key < 10000; key++)
    {
        detail_t* tmp = NULL;
        HASH_FIND_INT(td_local_summary->detail, &key, tmp); /* id already in the hash? */
        if (tmp == NULL)
        {
            tmp = (detail_t*)calloc(1, sizeof(detail_t));
            tmp->key = key;
            HASH_ADD_INT(td_local_summary->detail, key, tmp);
        }

        tmp->value++;
    }

    return NULL;
}


void calc_sum()
{
    const int THREAD_NUM = 100;
    pthread_t pid[THREAD_NUM];
    for (int i = 0; i < THREAD_NUM; i++)
    {
        pthread_create(pid + i, NULL, calc, NULL);
    }
    for (int i = 0; i < THREAD_NUM; i++)
    {
        pthread_join(pid[i], NULL);
    }

    uint64_t sum = 0;
    summary_t* iter;
    printf("sumary num: %d\n", HASH_COUNT(summary));
    for (iter = summary; iter != NULL; iter = (summary_t*)(iter->hh.next))
    {
        printf("detail num: %d\n", HASH_COUNT(iter->detail));
        detail_t* detail;
        for (detail = iter->detail; detail != NULL; detail = (detail_t*)(detail->hh.next))
        {
            sum += detail->value;
        }
    }

    printf("sum = %lld\n", sum);

    // delete all
    for (iter = summary; iter != NULL; iter = (summary_t*)(iter->hh.next))
    {
        detail_t *current_detail;
        detail_t *tmp;

        HASH_ITER(hh, iter->detail, current_detail, tmp) {
            HASH_DEL(iter->detail, current_detail);  /* delete it (users advances to next) */
            free(current_detail);             /* free it */
        }
    }

    summary_t *current_summary;
    HASH_ITER(hh, summary, current_summary, iter) {
        HASH_DEL(summary, current_summary);
        free(current_summary);
    }
}

int main(int argc, char *argv[])
{
    calc_sum();
    return 0;
}