#include "ap/platform.hpp"
#include "ap/xj380_adapter.h"
#include <stdio.h>
using namespace ap;
namespace {
ap_xj380_adapter_v1 adapter{};
bool enabled = true, growing = false;
int reads = 0, failures = 0;
Str stored = "{\"exists\":true,\"value\":\"native-value\"}";
void check(bool condition, const char* name) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}
int64_t read(const char*, char* output, uint64_t capacity, uint64_t* needed) {
    ++reads;
    *needed = stored.size() + 1;
    if (growing && !output)
        *needed = 2;
    if (!output || capacity < *needed)
        return -28;
    ap_memcpy(output, stored.c_str(), size_t(*needed));
    return 0;
}
int64_t write(const char*, const char* snapshot) {
    stored = snapshot;
    return 0;
}
int64_t shortcut(const char*, const char*, const char* const*, const char*, const char*) {
    return 0;
}
} // namespace
extern "C" const ap_xj380_adapter_v1* ap_xj380_query_adapter_v1() {
    return enabled ? &adapter : nullptr;
}
int main() {
    enabled = false;
    check(os::nativeCapabilities() == 0 && os::shortcutExtension() == ".elf", "No adapter fallback");
    enabled = true;
    adapter.version = 2;
    adapter.bytes = sizeof(adapter);
    adapter.capabilities = 15;
    check(os::nativeCapabilities() == 0, "Unsupported ABI rejected");
    adapter.version = 1;
    check(os::nativeCapabilities() == 0, "Missing callbacks mask capabilities");
    adapter.read_setting = read;
    adapter.write_setting = write;
    adapter.write_shortcut = shortcut;
    adapter.shortcut_extension = ".xlink";
    check(os::nativeCapabilities() == 7 && os::shortcutExtension() == ".xlink", "Available native callbacks");
    adapter.shortcut_extension = ".unsafe/link";
    check(!(os::nativeCapabilities() & AP_XJ380_CAP_SHORTCUTS),
          "Invalid extension disables native shortcuts");
    Json descriptor = Json::object(), snapshot;
    descriptor.set("kind", Json("environment"));
    descriptor.set("name", Json("DEMO_HOME"));
    descriptor.set("scope", Json("user"));
    check(bool(os::nativeRead(descriptor, snapshot)) && snapshot.str("value") == "native-value",
          "Two phase UTF-8 setting snapshot");
    growing = true;
    reads = 0;
    check(bool(os::nativeRead(descriptor, snapshot)) && reads == 3, "Snapshot resizing retries safely");
    growing = false;
    Json before;
    check(bool(parseJson("{\"value\":\"native-value\",\"exists\":true}", before)) && before.same(snapshot),
          "Snapshot ownership ignores JSON key order");
    check(bool(os::nativeWrite(descriptor, before)), "Snapshot write round trip");
    stored = "{\"value\":\"missing-exists\"}";
    check(!os::nativeRead(descriptor, snapshot), "Invalid native snapshot rejected");
    stored = "{invalid";
    check(!os::nativeRead(descriptor, snapshot), "Malformed native JSON rejected");
    printf("XJ380 adapter contract: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
