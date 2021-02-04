#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "mem_hook.h"

struct A
{
    A()
    {
        p = new int;
    }

    ~A() { delete p; }

    int* p;
};

void test5()
{
    char* p = new char[100];
    delete[] p;
}

void test4()
{
    test5();
}

void test3()
{
    test4();
}

void test2()
{
    test3();
}

void test1()
{
    test2();
}

#include "mem_hook.h"

int main(int argc, char* argv[])
{
    mem_hook_init("192.168.88.1", 9000);
    int* p = (int*)malloc(10);
    free(p);

    A* a = new A;
    //delete a;

    test1();

    getchar();

    mem_hook_deinit();
    return 0;
}