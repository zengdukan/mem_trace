## 统计内存泄漏方案:
1. 进程只收集malloc/free 数据并通过udp发出，另有分析进程负责分析内存泄漏，解析地址。用test_cpu测试udp发送，单次malloc增加耗时60us

2. 进程收集发送 malloc/free 数据, 并分析内存泄漏，最终把统计结果通过udp发出.用test_cpu测试, 单次malloc增加耗时46us
    - ~~支持多线程但不加锁~~, 必须加锁, 有可能在A线程分配, 在B线程释放
    - ~~1个c map保存线程bt数据~~
    - ~~1个c map保存多线程数据~~

## 计划

| 时间         | 事项                                             | 说明                                                                                                                            |
| ---------- | ---------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------- |
| 2024/05/24 | 测试backtrace和__builtin_frame_address cpu使用率 | 1. backtrace获取的地址addr2line取法解析，要用dladdr1转换且-g 编译 </br> 2.   __builtin_frame_address 同获取的地址addr2line取法解析，要用dladdr1转换且-g 编译 </br> 3. 测试__builtin_frame_address耗时远低于backtrace, 前者=0.01us, 后者=1.51us |
| 2024/05.29           | c内存泄漏检测库:检测malloc/free                         |  ok                                                                                                                             |
| 2024/05/29           | c内存泄漏检测库:获取call stack                          | ok                                                                                                                              |
| 2024/05/30           | c内存泄漏检测库:支持设置udp端口/开始/结束                       | ok                                                                                                                              |
| 2024/05/30           | c内存泄漏检测库: 通过udp发送出去                            |  ok                                                                                                                             |
|            | c内存泄漏统计:udp接收数据                               |                                                                                                                               |
|            | c内存泄漏统计:分析泄漏情况                                 |                                                                                                                               |

## 资料

### 1. 获取调用栈

参考[链接](https://www.cnblogs.com/utopia007/p/11642581.html)
```
Obtaining the backtrace - libunwind
I'm aware of three reasonably well-known methods of accessing the call stack programmatically:

The gcc builtin macro __builtin_return_address: very crude, low-level approach. This obtains the return address of the function on each frame on the stack. Note: just the address, not the function name. So extra processing is required to obtain the function name.
glibc's backtrace and backtrace_symbols: can obtain the actual symbol names for the functions on the call stack.
libunwind
Between the three, I strongly prefer libunwind, as it's the most modern, widespread and portable solution. It's also more flexible than backtrace, being able to provide extra information such as values of CPU registers at each stack frame.

Moreover, in the zoo of system programming, libunwind is the closest to the "official word" you can get these days. For example, gcc can use libunwind for implementing zero-cost C++ exceptions (which requires stack unwinding when an exception is actually thrown) [1]. LLVM also has a re-implementation of the libunwind interface in libc++, which is used for unwinding in LLVM toolchains based on this library.
```

