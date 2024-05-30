#include <csignal>
#include <cstdint>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

void func_free()
{
    void *p = malloc(100);
    free(p);
}


int main(int argc, char *argv[])
{
    getchar();

    struct timeval tv1, tv2;
    int64_t ts1, ts2;
    const int count = 1000000;

    gettimeofday(&tv1, NULL);
    for (int i = 0; i < count; ++i)
    {
        func_free();
    }
    gettimeofday(&tv2, NULL);

    ts1 = ((int64_t)tv1.tv_sec) * 1000000 + tv1.tv_usec;
    ts2 = ((int64_t)tv2.tv_sec) * 1000000 + tv2.tv_usec;
    int64_t diff = ts2 - ts1;
    printf("time: %f us\n", (double)diff / count);

    return 0;
}