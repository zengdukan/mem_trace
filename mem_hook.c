#include "mem_hook.h"

#include <stdio.h>
#include <malloc.h>
#include <execinfo.h>
#include <time.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <string.h>

/*
1. make .a or .so
2. send info by udp
*/

#define MAX_BT_SIZE 1024

typedef struct send_data_s
{
    uint64_t timestamp; // us
    uint64_t mem_addr;
    uint64_t mem_size;
    uint64_t bt_size;
    char bt[MAX_BT_SIZE];
    struct send_data_s* next;
} send_data_t;

typedef struct mem_hook_s
{
    int fd;
    int th_run;
    pthread_t th_id;
    struct sockaddr_in addr;
    send_data_t* send_list_hdr;
    send_data_t* send_list_tail;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} mem_hook_t;


int mem_hook_init(const char* ip, uint16_t port);
void mem_hook_deinit();
static void* thread_work(void*);
static void add_data(uint64_t mem_addr, uint64_t mem_size, int bt_array_size, const char** bt_array);

static mem_hook_t g_mem_hook;

/* Prototypes for our hooks.  */
static void my_init_hook(void);
static void *my_malloc_hook(size_t, const void *);
static void my_free_hook(void *ptr, const void *caller);

/* Variables to save original hooks. */
static void *(*old_malloc_hook)(size_t, const void *);
static void (*old_free_hook)(void *ptr, const void *caller);

/* Override initializing hook from the C library. */
// void (*__MALLOC_HOOK_VOLATILE __malloc_initialize_hook) (void) = my_init_hook;

static void my_init_hook(void)
{
    old_malloc_hook = __malloc_hook;
    __malloc_hook = my_malloc_hook;

    old_free_hook = __free_hook;
    __free_hook = my_free_hook;
}

static void my_deinit_hook(void)
{
    __malloc_hook = old_malloc_hook;
    __free_hook = old_free_hook;
}

#define SIZE 100

static void *my_malloc_hook(size_t size, const void *caller)
{
    void *result;

    /* Restore all old hooks */
    __malloc_hook = old_malloc_hook;
    __free_hook = old_free_hook;

    /* Call recursively */
    result = malloc(size);

    /* Save underlying hooks */
    old_malloc_hook = __malloc_hook;
    old_free_hook = __free_hook;

    int j, nptrs;
    void *buffer[100];
    char **strings;

    nptrs = backtrace(buffer, SIZE);

    /* The call backtrace_symbols_fd(buffer, nptrs, STDOUT_FILENO)
        would produce similar output to the following: */
    strings = backtrace_symbols(buffer, nptrs);
    if (strings == NULL) {
        perror("backtrace_symbols");
        return result;
    }

#if 0
    /* printf() might call malloc(), so protect it too. */
    fprintf(stderr, "malloc(%u) called from %p returns %p\n", (unsigned int) size, caller, result);
    // for (j = 0; j < nptrs; j++)
    //     fprintf(stderr, "%s\n", strings[j]);
    // fprintf(stderr, "\n");
#else
    add_data((uint64_t)result, size, nptrs, (const char**)strings);
#endif


    free(strings);

    /* Restore our own hooks */
    __malloc_hook = my_malloc_hook;
    __free_hook = my_free_hook;

    return result;
}

static void my_free_hook(void *ptr, const void *caller)
{
    if (ptr == NULL)
        return;

    /* Restore all old hooks */
    __malloc_hook = old_malloc_hook;
    __free_hook = old_free_hook;
    free(ptr);
    
    /* Save underlying hooks */
    old_malloc_hook = __malloc_hook;
    old_free_hook = __free_hook;

#if 0
    fprintf(stderr, "free %p, called from %p\n", ptr, caller);
#else    
    add_data((uint64_t)ptr, (uint64_t)caller, 0, NULL);
#endif

    /* Restore our own hooks */
    __malloc_hook = my_malloc_hook;
    __free_hook = my_free_hook;
}



int mem_hook_init(const char* ip, uint16_t port)
{
    memset(&g_mem_hook, 0, sizeof(g_mem_hook));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip);
    addr.sin_port = htons(port);
    g_mem_hook.addr = addr;

    g_mem_hook.fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_mem_hook.fd <= 0)
        return -1;

    g_mem_hook.mutex = PTHREAD_MUTEX_INITIALIZER;
    g_mem_hook.cond = PTHREAD_COND_INITIALIZER;

    g_mem_hook.th_run = 1;
    pthread_create(&g_mem_hook.th_id, NULL, thread_work, NULL);

    my_init_hook();
    return 0;
}

void mem_hook_deinit()
{
    my_deinit_hook();

    if (g_mem_hook.th_run)
    {
        g_mem_hook.th_run = 0;
        pthread_join(g_mem_hook.th_id, NULL);
    }

    if (g_mem_hook.fd > 0)
    {
        close(g_mem_hook.fd);
        g_mem_hook.fd = 0;
    }

    while (g_mem_hook.send_list_hdr)
    {
        send_data_t* next = g_mem_hook.send_list_hdr->next;
        free(g_mem_hook.send_list_hdr);
        g_mem_hook.send_list_hdr = next;
    }

    pthread_cond_destroy(&g_mem_hook.cond);
    pthread_mutex_destroy(&g_mem_hook.mutex);
}

static void udp_send_data()
{
    send_data_t* send_data = g_mem_hook.send_list_hdr;

    int size = 32 + send_data->bt_size;
    int ret = sendto(g_mem_hook.fd, (const void*)send_data, size, MSG_NOSIGNAL, (const struct sockaddr *)&g_mem_hook.addr, sizeof(struct sockaddr));
    if (ret < 0)
        fprintf(stderr, "sendto fail:%d\n", ret);

    g_mem_hook.send_list_hdr = send_data->next;
    if (g_mem_hook.send_list_hdr == NULL)
        g_mem_hook.send_list_tail = NULL;
    
    __free_hook = old_free_hook;    
    free(send_data);
    old_free_hook = __free_hook;
    __free_hook = my_free_hook;
}

void* thread_work(void* args)
{
    struct timeval now;
    struct timespec timeout;
    int retcode;

    while (g_mem_hook.th_run)
    {
        pthread_mutex_lock(&g_mem_hook.mutex);
        gettimeofday(&now, NULL);
        timeout.tv_sec = now.tv_sec + 1;
        timeout.tv_nsec = now.tv_usec * 1000;
        if (g_mem_hook.send_list_hdr == NULL)
        {
            retcode = pthread_cond_timedwait(&g_mem_hook.cond, &g_mem_hook.mutex, &timeout);
            if (retcode != ETIMEDOUT) {
                udp_send_data();
            }
        }
        else
            udp_send_data();
        pthread_mutex_unlock(&g_mem_hook.mutex);
    }
}

void add_data(uint64_t mem_addr, uint64_t mem_size, int bt_array_size, const char** bt_array)
{
    pthread_mutex_lock(&g_mem_hook.mutex);
    send_data_t* send_data = (send_data_t*)calloc(1, sizeof(send_data_t));

    struct timeval timeout;
    gettimeofday(&timeout, NULL);
    uint64_t usec = (uint64_t)timeout.tv_sec*1000000 + timeout.tv_usec;
    send_data->timestamp = usec;
    send_data->mem_size = mem_size;
    send_data->mem_addr = mem_addr;
    
    if (bt_array_size != 0)
    {
        int bt_idx = 0;
        char* ptr = send_data->bt;
        int bt_size = 0;
        int ret = 0;
        for (bt_idx = 0; bt_idx < bt_array_size; ++bt_idx) {
            int left = MAX_BT_SIZE - bt_size - 1;
            if (left <= 0)
                break;
       
            ret = snprintf(ptr + bt_size, left, "%s\r\n", bt_array[bt_idx]);
            bt_size += ret;
        }
        send_data->bt_size = bt_size;
    }

    if (g_mem_hook.send_list_tail == NULL)
    {
        g_mem_hook.send_list_tail = send_data;
        g_mem_hook.send_list_hdr = send_data;
    }
    else
    {
        g_mem_hook.send_list_tail->next = send_data;
        g_mem_hook.send_list_tail = send_data;
    }
    pthread_cond_broadcast(&g_mem_hook.cond);
    pthread_mutex_unlock(&g_mem_hook.mutex);
}

// int main(int argc, char **argv)
// {
//     int* p = (int*)malloc(sizeof(int));
//     free(p);
//     return 0;
// }
