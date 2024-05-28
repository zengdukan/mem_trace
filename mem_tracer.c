#define _GNU_SOURCE
#include <dlfcn.h>
#include <link.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define BT_SIZE 10

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

static pthread_once_t once = PTHREAD_ONCE_INIT;
__thread int trace = 1;

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
}

static void init()
{
    pthread_once(&once, &init_no_alloc_allowed);
}

static void enable()
{
    trace = 1;
}

static void disable()
{
    trace = 0;
}

static void *convert_addr(void *addr)
{
    Dl_info info;
    struct link_map *link;
    dladdr1(addr, &info, (void **)&link, RTLD_DL_LINKMAP);
    return (void *)((size_t)addr - link->l_addr);
}

static void bt()
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
    } while (0);

    // fill remaining spaces
    for (; index < BT_SIZE; index++)
        arr[index] = NULL;

    int i = 0;
    for (i = 0; i < index && arr[i] != NULL; i++)
    {
        arr[i] = convert_addr(arr[i]);
        printf("%p\n", arr[i]);
    }
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
    if (!trace)
        return libc_malloc(size);

    void *p = NULL;

    init();
    disable();

    p = libc_malloc(size);
    bt();

    enable();

    return p;
}

void free(void *ptr)
{
    if (!trace)
        libc_free(ptr);

    init();
    disable();

    libc_free(ptr);

    enable();
}

void *realloc(void *ptr, size_t size)
{
    if (!trace)
        libc_realloc(ptr, size);

    void *p = NULL;

    init();
    disable();

    p = libc_realloc(ptr, size);

    enable();

    if (p != ptr)
    {
        if (ptr)
        {
            //   leaktracer::MemoryTrace::GetInstance().registerRelease(ptr, false);
        }
        // leaktracer::MemoryTrace::GetInstance().registerAllocation(p, size, false);
    }
    else
    {
        // leaktracer::MemoryTrace::GetInstance().registerReallocation(p, size, false);
    }

    return p;
}

void *calloc(size_t nmemb, size_t size)
{
    if (!trace)
        libc_calloc(nmemb, size);

    void *p = NULL;

    init();
    disable();

    p = libc_calloc(nmemb, size);
    bt();

    enable();

    return p;
}
