#include "ap/base.hpp"
#if defined(AP_XJ380)
#include "ap/xj380.hpp"
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <stdlib.h>
#include <unistd.h>
#endif
extern "C" void* ap_allocate(size_t bytes) {
    if (!bytes)
        bytes = 1;
#if defined(AP_XJ380)
    return xapi_AllocateMemory(bytes);
#elif defined(_WIN32)
    return HeapAlloc(GetProcessHeap(), 0, bytes);
#else
    return malloc(bytes);
#endif
}
extern "C" void ap_deallocate(void* pointer) {
    if (!pointer)
        return;
#if defined(AP_XJ380)
    xapi_FreeMemory(pointer);
#elif defined(_WIN32)
    HeapFree(GetProcessHeap(), 0, pointer);
#else
    free(pointer);
#endif
}
extern "C" [[noreturn]] void ap_panic(const char* message) {
#if defined(AP_XJ380)
    xapi_Output(const_cast<char*>(message));
    xapi_Exit(70);
#elif defined(_WIN32)
    MessageBoxA(nullptr, message, "Accretion Package", MB_OK | MB_ICONERROR);
    ExitProcess(70);
#else
    auto written = ::write(2, message, ap::length(message));
    written = ::write(2, "\n", 1);
    (void)written;
    _exit(70);
#endif
    for (;;) {
    }
}
extern "C" void* ap_memcpy(void* to, const void* from, size_t size) {
    auto* d = static_cast<unsigned char*>(to);
    auto* s = static_cast<const unsigned char*>(from);
    for (size_t i = 0; i < size; ++i)
        d[i] = s[i];
    return to;
}
extern "C" void* ap_memmove(void* to, const void* from, size_t size) {
    auto* d = static_cast<unsigned char*>(to);
    auto* s = static_cast<const unsigned char*>(from);
    if (d < s) {
        for (size_t i = 0; i < size; ++i)
            d[i] = s[i];
    } else if (d > s) {
        while (size) {
            --size;
            d[size] = s[size];
        }
    }
    return to;
}
extern "C" void* ap_memset(void* to, int value, size_t size) {
    auto* d = static_cast<unsigned char*>(to);
    for (size_t i = 0; i < size; ++i)
        d[i] = static_cast<unsigned char>(value);
    return to;
}
