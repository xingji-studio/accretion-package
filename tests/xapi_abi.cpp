#include "ap/xj380.hpp"
#include <stdio.h>
namespace {
uint64_t last = 0, args[6]{}, result = 0;
int failed = 0;
void check(bool good, const char* name) {
    if (!good) {
        fprintf(stderr, "XAPI ABI mismatch: %s\n", name);
        ++failed;
    }
}
} // namespace
extern "C" uint64_t ap_xapi_call(uint64_t number, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e,
                                 uint64_t f) {
    last = number;
    args[0] = a;
    args[1] = b;
    args[2] = c;
    args[3] = d;
    args[4] = e;
    args[5] = f;
    return result;
}
int main() {
    char path[] = "/apps/example/app.elf", buffer[20]{};
    XWindow window{800, 600, path, 0};
    XHandle handle = 0;
    char* argv[] = {path, nullptr};
    char* envp[] = {path, nullptr};
    xapi_CreateWindow(&handle, &window);
    check(last == 7392 && args[0] == uint64_t(&handle) && args[1] == uint64_t(&window), "CreateWindow");
    xapi_ReadFile(path, buffer, 20, 4096);
    check(last == 7423 && args[0] == uint64_t(path) && args[1] == uint64_t(buffer) && args[2] == 20 &&
              args[3] == 4096,
          "ReadFile");
    result = uint64_t(-5);
    check(xapi_WriteFile(path, buffer, 20, 100) == uint64_t(-5) && last == 7424 && args[3] == 100,
          "WriteFile error encoding");
    result = 0;
    xapi_QueryFileAccess(path, 64 | 16 | 2);
    check(last == 7500 && args[1] == 82, "QueryFileAccess");
    xapi_RequestFileAccess(path, 82);
    check(last == 7501, "RequestFileAccess");
    xapi_DrawText(42, 10, 20, path, 18, 0x123456);
    check(last == 7402 && args[0] == 42 && args[3] == uint64_t(path) && args[4] == 18 && args[5] == 0x123456,
          "DrawText");
    xapi_DrawRect(42, 1, 2, 3, 4, 0xabcdef, true);
    check(last == 7399 && args[3] == 3 && args[5] == 0xabcdef, "filled rectangle");
    xapi_DrawRect(42, 1, 2, 3, 4, 0, false);
    check(last == 7398, "rectangle");
    xapi_PutTextInputBox(42, 1, 2, 600, path);
    check(last == 7456 && args[3] == 600 && args[4] == uint64_t(path), "text input");
    xapi_Fork();
    check(last == 7389, "native fork");
    xapi_Execve(path, argv, envp);
    check(last == 7390 && args[1] == uint64_t(argv) && args[2] == uint64_t(envp),
          "native execve environment");
    xapi_LiftPermissions(3);
    check(last == 7496 && args[0] == 3, "Visitor launch permissions");
    result = 0x100000;
    check(xapi_AllocateMemory(99) == reinterpret_cast<void*>(0x100000) && last == 7441 && args[0] == 99,
          "native memory allocation");
    xapi_FreeMemory(reinterpret_cast<void*>(0x100000));
    check(last == 7442, "native memory free");
    puts(failed ? "Native XAPI ABI checks FAILED" : "Native XAPI ABI checks passed");
    return failed ? 1 : 0;
}
