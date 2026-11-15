#pragma once
#include <stddef.h>
#include <stdint.h>

// Native XAPI 1.4 ABI subset. Audited against XJ380-SDK
// 91df024ff5f346f4870b3dc042cbc0e5bf06a046. No POSIX entry points.
using XHandle = uint64_t;
struct XWindow {
    uint32_t width, height;
    char* title;
    uint8_t sets;
};
struct XDirectoryEntry {
    char filename[256];
    uint64_t length, filetype;
};
struct XUser {
    char name[64];
    uint32_t type;
};
using XMessage = void (*)(uint64_t, uint64_t, uint64_t);
static_assert(sizeof(XWindow) == 24 && offsetof(XWindow, title) == 8, "XAPI window ABI");
static_assert(sizeof(XDirectoryEntry) == 272 && sizeof(XUser) == 68, "XAPI directory/user ABI");
extern "C" {
uint64_t ap_xapi_call(uint64_t number, uint64_t a = 0, uint64_t b = 0, uint64_t c = 0, uint64_t d = 0,
                      uint64_t e = 0, uint64_t f = 0);
void* xapi_AllocateMemory(uint64_t bytes);
void xapi_FreeMemory(void* pointer);
void xapi_Output(char* text);
void xapi_Getline(char* buffer);
[[noreturn]] void xapi_Exit(uint64_t status);
void xapi_Sleep(uint64_t milliseconds);
uint64_t xapi_GetTimeNano();
void xapi_GetCurrentUser(XUser* user);
void xapi_SearchFile(char* path, int32_t* count, XDirectoryEntry** entries);
void xapi_SearchFile_freem(XDirectoryEntry* entries, int32_t count);
uint64_t xapi_Mkdir(char* path);
uint64_t xapi_CreateFile(char* path);
uint64_t xapi_DeleteFile(char* path);
uint64_t xapi_Rmdir(char* path);
uint64_t xapi_RenameFile(char* from, char* to);
uint64_t xapi_ReadFile(char* path, char* buffer, uint64_t bytes, uint64_t offset);
uint64_t xapi_WriteFile(char* path, char* buffer, uint64_t bytes, uint64_t offset);
int64_t xapi_QueryFileAccess(const char* path, uint64_t access);
int64_t xapi_RequestFileAccess(const char* path, uint64_t access);
int xapi_LiftPermissions(unsigned type);
uint64_t xapi_Fork();
uint64_t xapi_Execve(char* path, char** arguments, char** environment);
void xapi_CreateWindow(XHandle* handle, XWindow* window);
void xapi_CloseWindow(XHandle handle);
void xapi_SetIcon(XHandle handle, char* path);
void SetMsgPrcor(XHandle handle, XMessage callback);
void xapi_DrawRect(XHandle handle, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t color,
                   bool fill);
void xapi_DrawLine(XHandle handle, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t color);
void xapi_DrawText(XHandle handle, uint32_t x, uint32_t y, char* text, uint32_t size, uint32_t color);
void xapi_RefreshWindow(XHandle handle);
uint64_t xapi_PutTextInputBox(XHandle handle, uint64_t x, uint64_t y, uint64_t width, char* text);
void xapi_GetTextInputBox(uint64_t id, char* buffer);
void xapi_DeleteTextInputBox(uint64_t id);
}
