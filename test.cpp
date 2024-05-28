
#include <memory>
#include <stdlib.h>

void func_free(int argc)
{
    void *p = malloc(100);
    free(p);
}

void func_no_free()
{
    void *p = malloc(100);
}

void func_new()
{
    int *p = new int;
}

void func_unique_ptr()
{
    std::unique_ptr<int> p = std::make_unique<int>();
}

void func2(int argc)
{
    // func_free(argc);
    // func_no_free();
    func_new();
}

void func1(int argc)
{
    func2(argc);
}

int main(int argc, char *argv[])
{
    func1(argc);
    return 0;
}