#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void* ap_memcpy(void*, const void*, size_t);
void* ap_memmove(void*, const void*, size_t);
void* ap_memset(void*, int, size_t);
#ifdef __cplusplus
}
#endif
#define LZ4_FREESTANDING 1
#define LZ4_memcpy ap_memcpy
#define LZ4_memmove ap_memmove
#define LZ4_memset ap_memset
