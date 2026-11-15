#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define AP_XJ380_ADAPTER_VERSION 1u
#define AP_XJ380_CAP_ENVIRONMENT 1ull
#define AP_XJ380_CAP_SHORTCUTS 2ull
#define AP_XJ380_CAP_ASSOCIATIONS 4ull
#define AP_XJ380_CAP_LAUNCH 8ull

/* read_setting returns a UTF-8 JSON snapshot {"exists":bool,"value":...}.
 * Query with buffer=NULL/capacity=0; needed includes the trailing NUL.
 * Return 0 or -28 (insufficient buffer), other negative values are errors.
 * Descriptors have kind, scope and name/id; snapshots must round-trip unchanged.
 * All callbacks run on the installer's stable main user thread and must use
 * native XAPI. They must not grant permissions beyond the OS authorization flow.
 */
typedef struct ap_xj380_adapter_v1 {
    uint32_t version, bytes;
    uint64_t capabilities;
    int64_t (*read_setting)(const char* descriptor, char* buffer, uint64_t capacity, uint64_t* needed);
    int64_t (*write_setting)(const char* descriptor, const char* snapshot);
    int64_t (*write_shortcut)(const char* output, const char* executable, const char* const* arguments,
                              const char* working_directory, const char* icon);
    const char* shortcut_extension; /* e.g. ".link"; immutable UTF-8, no slashes */
    const char* (*desktop_directory)(uint32_t machine);
    int64_t (*launch)(const char* executable, const char* const* arguments, const char* const* environment,
                      const char* working_directory, uint32_t visitor);
    void (*changed)(void);
} ap_xj380_adapter_v1;

/* Supply a strong definition from your SDK adapter. The built-in weak
 * implementation returns NULL. No undocumented syscall numbers are reserved.
 */
const ap_xj380_adapter_v1* ap_xj380_query_adapter_v1(void);
#ifdef __cplusplus
}
static_assert(sizeof(ap_xj380_adapter_v1) == 72, "XJ380 adapter ABI requires x86-64");
#endif
