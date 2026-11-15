#include "ap/xj380.hpp"
#ifndef AP_XAPI_TEST
extern "C" uint64_t ap_xapi_call(uint64_t number, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e,
                                 uint64_t f) {
    register uint64_t r10 asm("r10") = d;
    register uint64_t r8 asm("r8") = e;
    register uint64_t r9 asm("r9") = f;
    uint64_t result;
    asm volatile("syscall"
                 : "=a"(result)
                 : "a"(number), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
                 : "rcx", "r11", "memory");
    return result;
}
#endif
extern "C" {
void* xapi_AllocateMemory(uint64_t bytes) {
    uint64_t result = ap_xapi_call(7441, bytes);
    return int64_t(result) < 0 ? nullptr : reinterpret_cast<void*>(result);
}
void xapi_FreeMemory(void* pointer) {
    if (pointer)
        ap_xapi_call(7442, reinterpret_cast<uint64_t>(pointer));
}
void xapi_Output(char* text) {
    ap_xapi_call(7381, reinterpret_cast<uint64_t>(text));
}
void xapi_Getline(char* buffer) {
    ap_xapi_call(7418, reinterpret_cast<uint64_t>(buffer));
}
// This is the native xapi_Exit ABI used by the official SDK, not libc exit().
[[noreturn]] void xapi_Exit(uint64_t status) {
    ap_xapi_call(60, status);
    for (;;) {
    }
}
void xapi_Sleep(uint64_t ms) {
    ap_xapi_call(7430, ms);
}
uint64_t xapi_GetTimeNano() {
    return ap_xapi_call(7459);
}
void xapi_GetCurrentUser(XUser* user) {
    ap_xapi_call(7413, reinterpret_cast<uint64_t>(user));
}
void xapi_SearchFile(char* path, int32_t* count, XDirectoryEntry** entries) {
    ap_xapi_call(7416, reinterpret_cast<uint64_t>(path), reinterpret_cast<uint64_t>(count),
                 reinterpret_cast<uint64_t>(entries));
}
void xapi_SearchFile_freem(XDirectoryEntry* entries, int32_t count) {
    ap_xapi_call(7471, reinterpret_cast<uint64_t>(entries), uint64_t(count));
}
uint64_t xapi_Mkdir(char* path) {
    return ap_xapi_call(7425, reinterpret_cast<uint64_t>(path));
}
uint64_t xapi_CreateFile(char* path) {
    return ap_xapi_call(7420, reinterpret_cast<uint64_t>(path));
}
uint64_t xapi_DeleteFile(char* path) {
    return ap_xapi_call(7421, reinterpret_cast<uint64_t>(path));
}
uint64_t xapi_Rmdir(char* path) {
    return ap_xapi_call(7444, reinterpret_cast<uint64_t>(path));
}
uint64_t xapi_RenameFile(char* from, char* to) {
    return ap_xapi_call(7422, reinterpret_cast<uint64_t>(from), reinterpret_cast<uint64_t>(to));
}
uint64_t xapi_ReadFile(char* path, char* buffer, uint64_t bytes, uint64_t offset) {
    return ap_xapi_call(7423, reinterpret_cast<uint64_t>(path), reinterpret_cast<uint64_t>(buffer), bytes,
                        offset);
}
uint64_t xapi_WriteFile(char* path, char* buffer, uint64_t bytes, uint64_t offset) {
    return ap_xapi_call(7424, reinterpret_cast<uint64_t>(path), reinterpret_cast<uint64_t>(buffer), bytes,
                        offset);
}
int64_t xapi_QueryFileAccess(const char* path, uint64_t access) {
    return int64_t(ap_xapi_call(7500, reinterpret_cast<uint64_t>(path), access));
}
int64_t xapi_RequestFileAccess(const char* path, uint64_t access) {
    return int64_t(ap_xapi_call(7501, reinterpret_cast<uint64_t>(path), access));
}
int xapi_LiftPermissions(unsigned type) {
    return int(ap_xapi_call(7496, type));
}
uint64_t xapi_Fork() {
    return ap_xapi_call(7389);
}
uint64_t xapi_Execve(char* path, char** args, char** env) {
    return ap_xapi_call(7390, reinterpret_cast<uint64_t>(path), reinterpret_cast<uint64_t>(args),
                        reinterpret_cast<uint64_t>(env));
}
void xapi_CreateWindow(XHandle* handle, XWindow* window) {
    ap_xapi_call(7392, reinterpret_cast<uint64_t>(handle), reinterpret_cast<uint64_t>(window));
}
void xapi_CloseWindow(XHandle handle) {
    ap_xapi_call(7394, handle);
}
void xapi_SetIcon(XHandle handle, char* path) {
    ap_xapi_call(7395, handle, reinterpret_cast<uint64_t>(path));
}
void SetMsgPrcor(XHandle handle, XMessage callback) {
    ap_xapi_call(7405, handle, reinterpret_cast<uint64_t>(callback));
}
void xapi_DrawRect(XHandle handle, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t color,
                   bool fill) {
    ap_xapi_call(fill ? 7399 : 7398, handle, x1, y1, x2, y2, color);
}
void xapi_DrawLine(XHandle handle, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t color) {
    ap_xapi_call(7397, handle, x1, y1, x2, y2, color);
}
void xapi_DrawText(XHandle handle, uint32_t x, uint32_t y, char* text, uint32_t size, uint32_t color) {
    ap_xapi_call(7402, handle, x, y, reinterpret_cast<uint64_t>(text), size, color);
}
void xapi_RefreshWindow(XHandle handle) {
    ap_xapi_call(7409, handle);
}
uint64_t xapi_PutTextInputBox(XHandle handle, uint64_t x, uint64_t y, uint64_t width, char* text) {
    return ap_xapi_call(7456, handle, x, y, width, reinterpret_cast<uint64_t>(text));
}
void xapi_GetTextInputBox(uint64_t id, char* buffer) {
    ap_xapi_call(7457, id, reinterpret_cast<uint64_t>(buffer));
}
void xapi_DeleteTextInputBox(uint64_t id) {
    ap_xapi_call(7458, id);
}
}
