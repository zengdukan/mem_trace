#ifndef __MEM_HOOK_H_
#define __MEM_HOOK_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 0:success, -1:fail
int mem_hook_init(const char* ip, uint16_t port);

void mem_hook_deinit();


#ifdef __cplusplus
}
#endif

#endif // __MEM_HOOK_H_