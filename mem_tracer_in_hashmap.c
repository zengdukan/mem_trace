#define _GNU_SOURCE
#include <dlfcn.h>
#include <link.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h> /* See NOTES */

#include "uthash.h"

#define BT_SIZE 20

// glibc/eglibc: dlsym uses calloc internally now, so use weak symbol to get their symbol
extern void *__libc_malloc(size_t size) __attribute__((weak));
extern void __libc_free(void *ptr) __attribute__((weak));
extern void *__libc_realloc(void *ptr, size_t size) __attribute__((weak));
extern void *__libc_calloc(size_t nmemb, size_t size) __attribute__((weak));

typedef void *(*malloc_t)(size_t size);
typedef void (*free_t)(void *ptr);
typedef void *(*realloc_t)(void *ptr, size_t size);
typedef void *(*calloc_t)(size_t nmemb, size_t size);

static malloc_t libc_malloc = NULL;
static calloc_t libc_calloc = NULL;
static realloc_t libc_realloc = NULL;
static free_t libc_free = NULL;

typedef struct
{
    const char *symbname;
    void *libcsymbol;
    void **localredirect;
} libc_alloc_func_t;

static libc_alloc_func_t libc_alloc_funcs[] = {{"calloc", (void *)__libc_calloc, (void **)(&libc_calloc)},
                                               {"malloc", (void *)__libc_malloc, (void **)(&libc_malloc)},
                                               {"realloc", (void *)__libc_realloc, (void **)(&libc_realloc)},
                                               {"free", (void *)__libc_free, (void **)(&libc_free)}};

typedef struct bt_info_s
{
    uint64_t _mem_addr;
    uint64_t _mem_size;
    uint64_t _call_stack[BT_SIZE];
} bt_info_t;

typedef struct memory_info_s
{
    uint64_t _mem_addr;
    bt_info_t _bt_info;
    UT_hash_handle hh;
} memory_info_t;

static pthread_once_t once = PTHREAD_ONCE_INIT;
static int enable = 0;
static int notify_udp_port = 0;
static pthread_mutex_t memory_mutex = PTHREAD_MUTEX_INITIALIZER;
static memory_info_t *g_mem_info = NULL;
__thread int interval_switch = 1;

static void set_enable(int value)
{
    enable = value;
}

static void start_trace_signal_handler(int signo, siginfo_t *info, void *context)
{
    set_enable(1);
}

static void stop_trace_signal_handler(int signo, siginfo_t *info, void *context)
{
    set_enable(0);

    int notify_udp_fd = 0;
    struct sockaddr_in notify_udp_addr = {0};

    if (notify_udp_fd > 0)
    {
        close(notify_udp_fd);
        notify_udp_fd = 0;
    }

    memset(&notify_udp_addr, 0, sizeof(struct sockaddr_in));
    notify_udp_addr.sin_family = AF_INET;
    notify_udp_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    notify_udp_addr.sin_port = htons(notify_udp_port);
    notify_udp_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (notify_udp_fd <= 0)
    {
        perror("create memory tracer analyze udp fail: ");
        return;
    }

    // send
    memory_info_t* iter = NULL;
    for (iter = g_mem_info; iter != NULL; iter = (memory_info_t*)(iter->hh.next))
    {
        sendto(notify_udp_fd, (void*)&iter->_bt_info, sizeof(bt_info_t), 0, (const struct sockaddr *)&notify_udp_addr, sizeof(struct sockaddr));
    }

    // free
    memory_info_t* tmp;
    HASH_ITER(hh, g_mem_info, tmp, iter) {
        HASH_DEL(g_mem_info, tmp);
        free(tmp);
    }

    if (notify_udp_fd > 0)
    {
        close(notify_udp_fd);
        notify_udp_fd = 0;
    }
}

static void init_no_alloc_allowed()
{
    libc_alloc_func_t *func = NULL;
    int i = 0;
    int size = sizeof(libc_alloc_funcs) / sizeof(libc_alloc_funcs[0]);

    for (i = 0; i < size; ++i)
    {
        func = &libc_alloc_funcs[i];
        if (!*func->localredirect)
        {
            if (func->libcsymbol)
            {
                *func->localredirect = func->libcsymbol;
            }
            else
            {
                *func->localredirect = dlsym(RTLD_NEXT, func->symbname);
            }
        }
    }

    char *env = NULL;
    if ((env = getenv("MEM_TRACER_START_ONSIG")) != NULL)
    {
        struct sigaction act = {0};
        act.sa_sigaction = start_trace_signal_handler;
        sigemptyset(&act.sa_mask);
        act.sa_flags = SA_SIGINFO;
        int n = atoi(env);
        sigaction(n, &act, NULL);
    }

    if ((env = getenv("MEM_TRACER_STOP_ONSIG")) != NULL)
    {
        struct sigaction act = {0};
        act.sa_sigaction = stop_trace_signal_handler;
        sigemptyset(&act.sa_mask);
        act.sa_flags = SA_SIGINFO;
        int n = atoi(env);
        sigaction(n, &act, NULL);
    }

    if ((env = getenv("MEM_TRACER_UDP_PORT")) != NULL)
    {
        notify_udp_port = atoi(env);
    }
}

static void init()
{
    pthread_once(&once, &init_no_alloc_allowed);
}

static void open_interval_switch()
{
    interval_switch = 1;
}

static void close_interval_switch()
{
    interval_switch = 0;
}

static void *convert_addr(void *addr)
{
    Dl_info info;
    struct link_map *link;
    dladdr1(addr, &info, (void **)&link, RTLD_DL_LINKMAP);
    return (void *)((size_t)addr - link->l_addr);
}

static void bt(uint64_t call_stack[BT_SIZE])
{
    void *arr[BT_SIZE] = {0};
    int index = 0;
    void *frame = NULL;

    // NOTE: we can't use "for" loop, __builtin_* functions
    // require the number to be known at compile time
    do
    {
        arr[index++] = ((frame = __builtin_frame_address(0)) != NULL) ? __builtin_return_address(0) : NULL;
        if (index == BT_SIZE || frame == NULL)
            break;
        arr[index++] = (frame != NULL && (frame = __builtin_frame_address(1)) != NULL) ? __builtin_return_address(1) : NULL;
        if (index == BT_SIZE || frame == NULL)
            break;
        arr[index++] = (frame != NULL && (frame = __builtin_frame_address(2)) != NULL) ? __builtin_return_address(2) : NULL;
        if (index == BT_SIZE || frame == NULL)
            break;
        arr[index++] = (frame != NULL && (frame = __builtin_frame_address(3)) != NULL) ? __builtin_return_address(3) : NULL;
        if (index == BT_SIZE || frame == NULL)
            break;
        arr[index++] = (frame != NULL && (frame = __builtin_frame_address(4)) != NULL) ? __builtin_return_address(4) : NULL;
        if (index == BT_SIZE || frame == NULL)
            break;
        arr[index++] = (frame != NULL && (frame = __builtin_frame_address(5)) != NULL) ? __builtin_return_address(5) : NULL;
        if (index == BT_SIZE || frame == NULL)
            break;
        arr[index++] = (frame != NULL && (frame = __builtin_frame_address(6)) != NULL) ? __builtin_return_address(6) : NULL;
        if (index == BT_SIZE || frame == NULL)
            break;
        arr[index++] = (frame != NULL && (frame = __builtin_frame_address(7)) != NULL) ? __builtin_return_address(7) : NULL;
        if (index == BT_SIZE || frame == NULL)
            break;
        arr[index++] = (frame != NULL && (frame = __builtin_frame_address(8)) != NULL) ? __builtin_return_address(8) : NULL;
        if (index == BT_SIZE || frame == NULL)
            break;
        arr[index++] = (frame != NULL && (frame = __builtin_frame_address(9)) != NULL) ? __builtin_return_address(9) : NULL;
        if (index == BT_SIZE || frame == NULL)
            break;
        arr[index++] = (frame != NULL && (frame = __builtin_frame_address(10)) != NULL) ? __builtin_return_address(10) : NULL;
        if (index == BT_SIZE || frame == NULL)
            break;
        arr[index++] = (frame != NULL && (frame = __builtin_frame_address(11)) != NULL) ? __builtin_return_address(11) : NULL;
        if (index == BT_SIZE || frame == NULL)
            break;
        arr[index++] = (frame != NULL && (frame = __builtin_frame_address(12)) != NULL) ? __builtin_return_address(12) : NULL;
        if (index == BT_SIZE || frame == NULL)
            break;
        arr[index++] = (frame != NULL && (frame = __builtin_frame_address(13)) != NULL) ? __builtin_return_address(13) : NULL;
        if (index == BT_SIZE || frame == NULL)
            break;
        arr[index++] = (frame != NULL && (frame = __builtin_frame_address(14)) != NULL) ? __builtin_return_address(14) : NULL;
        if (index == BT_SIZE || frame == NULL)
            break;
        arr[index++] = (frame != NULL && (frame = __builtin_frame_address(15)) != NULL) ? __builtin_return_address(15) : NULL;
        if (index == BT_SIZE || frame == NULL)
            break;
        arr[index++] = (frame != NULL && (frame = __builtin_frame_address(16)) != NULL) ? __builtin_return_address(16) : NULL;
        if (index == BT_SIZE || frame == NULL)
            break;
        arr[index++] = (frame != NULL && (frame = __builtin_frame_address(17)) != NULL) ? __builtin_return_address(17) : NULL;
        if (index == BT_SIZE || frame == NULL)
            break;
        arr[index++] = (frame != NULL && (frame = __builtin_frame_address(18)) != NULL) ? __builtin_return_address(18) : NULL;
        if (index == BT_SIZE || frame == NULL)
            break;
        arr[index++] = (frame != NULL && (frame = __builtin_frame_address(19)) != NULL) ? __builtin_return_address(19) : NULL;
        if (index == BT_SIZE || frame == NULL)
            break;
    } while (0);

    // fill remaining spaces
    for (; index < BT_SIZE; index++)
        arr[index] = NULL;

    int i = 0;
    for (i = 0; i < BT_SIZE; i++)
    {
        if (arr[i] != NULL)
            arr[i] = convert_addr(arr[i]);
        call_stack[i] = (uint64_t)arr[i];
    }
}

static void analyze(void *mem_addr, size_t size, uint64_t call_stack[BT_SIZE])
{
    pthread_mutex_lock(&memory_mutex);

    if (size == 0) // free
    {
        memory_info_t *tmp = NULL;
        uint64_t tmp_addr = (uint64_t)mem_addr;
        HASH_FIND_INT(g_mem_info, &tmp_addr, tmp);
        if (tmp != NULL)
        {
            HASH_DEL(g_mem_info, tmp);
            free(tmp);
        }
    }
    else
    {
        memory_info_t *tmp = NULL;
        uint64_t _mem_addr = (uint64_t)mem_addr;
        HASH_FIND_INT(g_mem_info, &_mem_addr, tmp);
        if (tmp == NULL)
        {
            tmp = (memory_info_t *)calloc(1, sizeof(memory_info_t));
            tmp->_mem_addr = _mem_addr;
            HASH_ADD_INT(g_mem_info, _mem_addr, tmp);
        }

        tmp->_bt_info._mem_addr = _mem_addr;
        tmp->_bt_info._mem_size = size;
        memcpy(tmp->_bt_info._call_stack, call_stack, sizeof(tmp->_bt_info._call_stack));
    }

    pthread_mutex_unlock(&memory_mutex);
}

/** -- libc memory operators -- **/

/* malloc
 * in some malloc implementation, there is a recursive call to malloc
 * (for instance, in uClibc 0.9.29 malloc-standard )
 * we use a InternalMonitoringDisablerThreadUp that use a tls variable to
 * prevent several registration during the same malloc
 */
void *malloc(size_t size)
{
    init();

    if (!enable || !interval_switch)
        return libc_malloc(size);

    close_interval_switch();

    void *p = libc_malloc(size);
    if (p != NULL)
    {
        uint64_t call_stack[BT_SIZE] = {0};
        bt(call_stack);
        analyze(p, size, call_stack);
    }

    open_interval_switch();
    return p;
}

void free(void *ptr)
{
    init();

    if (!enable || !interval_switch)
    {
        libc_free(ptr);
        return;
    }

    close_interval_switch();

    libc_free(ptr);
    if (ptr != NULL)
    {
        analyze(ptr, 0, NULL);
    }

    open_interval_switch();
}

void *realloc(void *ptr, size_t size)
{
    init();

    if (!enable || !interval_switch)
        return libc_realloc(ptr, size);

    close_interval_switch();

    void *p = libc_realloc(ptr, size);
    if (p != NULL)
    {
        if (ptr != NULL)
        {
            analyze(ptr, 0, NULL);
        }

        uint64_t call_stack[BT_SIZE] = {0};
        bt(call_stack);
        analyze(p, size, call_stack);
    }

    open_interval_switch();
    return p;
}

void *calloc(size_t nmemb, size_t size)
{
    init();

    if (!enable || !interval_switch)
        return libc_calloc(nmemb, size);

    close_interval_switch();

    void *p = libc_calloc(nmemb, size);
    if (p != NULL)
    {
        uint64_t call_stack[BT_SIZE] = {0};
        bt(call_stack);
        analyze(p, nmemb * size, call_stack);
    }

    open_interval_switch();
    return p;
}
