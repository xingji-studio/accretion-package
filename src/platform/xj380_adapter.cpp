#include "ap/xj380_adapter.h"
#include "ap/platform.hpp"
#ifndef AP_XJ_ADAPTER_TEST
extern "C" __attribute__((weak)) const ap_xj380_adapter_v1* ap_xj380_query_adapter_v1() {
    return nullptr;
}
#endif
namespace ap::os {
namespace {
bool validExtension(const char* value) {
    if (!value || value[0] != '.' || !value[1])
        return false;
    for (size_t i = 1; value[i]; ++i) {
        char c = value[i];
        if (i >= 19 || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                         c == '_' || c == '-'))
            return false;
    }
    return true;
}
} // namespace
const ap_xj380_adapter_v1* xjAdapter() {
    const auto* adapter = ap_xj380_query_adapter_v1();
    return adapter && adapter->version == 1 && adapter->bytes >= sizeof(ap_xj380_adapter_v1) ? adapter
                                                                                             : nullptr;
}
uint64_t nativeCapabilities() {
    const auto* adapter = xjAdapter();
    if (!adapter)
        return 0;
    uint64_t capabilities = adapter->capabilities;
    if (!adapter->read_setting || !adapter->write_setting)
        capabilities &= ~(AP_XJ380_CAP_ENVIRONMENT | AP_XJ380_CAP_ASSOCIATIONS);
    if (!adapter->write_shortcut || !validExtension(adapter->shortcut_extension))
        capabilities &= ~AP_XJ380_CAP_SHORTCUTS;
    if (!adapter->launch)
        capabilities &= ~AP_XJ380_CAP_LAUNCH;
    return capabilities;
}
Status nativeRead(const Json& descriptor, Json& snapshot) {
    const auto* adapter = xjAdapter();
    if (!adapter || !adapter->read_setting)
        return Status::fail("XJ380 native setting adapter is unavailable");
    Str key = descriptor.dump();
    uint64_t needed = 0;
    int64_t code = adapter->read_setting(key.c_str(), nullptr, 0, &needed);
    if ((code != 0 && code != -28) || !needed || needed > 1024 * 1024)
        return Status::fail("XJ380 adapter returned an invalid snapshot size");
    for (unsigned retry = 0; retry < 3; ++retry) {
        Str data;
        data.resize(size_t(needed));
        uint64_t capacity = needed;
        code = adapter->read_setting(key.c_str(), data.data(), capacity, &needed);
        if (code == -28 && needed > capacity && needed <= 1024 * 1024)
            continue;
        if (code != 0 || !needed || needed > capacity || data[size_t(needed) - 1] != 0)
            return Status::fail("XJ380 adapter could not read native setting");
        data.resize(size_t(needed) - 1);
        AP_TRY(parseJson(data, snapshot));
        if (snapshot.kind != Json::Object || snapshot.get("exists").kind != Json::Bool)
            return Status::fail("XJ380 adapter snapshot needs exists:boolean");
        return {};
    }
    return Status::fail("XJ380 native setting changed during snapshot");
}
Status nativeWrite(const Json& descriptor, const Json& snapshot) {
    const auto* adapter = xjAdapter();
    if (!adapter || !adapter->write_setting)
        return Status::fail("XJ380 native setting adapter is unavailable");
    Str key = descriptor.dump(), value = snapshot.dump();
    int64_t result = adapter->write_setting(key.c_str(), value.c_str());
    return result == 0 ? Status{}
                       : Status::fail("XJ380 native setting write failed: " +
                                      number(result < 0 ? uint64_t(-result) : uint64_t(result)));
}
Str shortcutExtension() {
    if (nativeCapabilities() & AP_XJ380_CAP_SHORTCUTS)
        return xjAdapter()->shortcut_extension;
    return ".elf";
}
} // namespace ap::os
