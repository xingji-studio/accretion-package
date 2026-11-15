#if defined(AP_XJ380)
#include "ap/xj380.hpp"
extern "C" int ap_xj380_main(int, char**, char**) {
    xapi_Output(const_cast<char*>("Hello from the native XJ380 application!\n"));
    return 0;
}
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
int main() {
    const char message[] = "Hello from the Windows application!\r\n";
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, sizeof(message) - 1, &written, nullptr);
    return 0;
}
#else
#include <stdio.h>
int main() {
    puts("Hello from the Linux application!");
    return 0;
}
#endif
