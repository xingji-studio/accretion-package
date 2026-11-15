#include "ap/xj380_adapter.h"

// Replace capabilities and callbacks when the corresponding OS APIs are ready.
// A zero-capability adapter preserves the native ELF-launcher/application-env fallback.
// Callback signatures and JSON descriptors: docs/xj380-adapter.md.
extern "C" const ap_xj380_adapter_v1* ap_xj380_query_adapter_v1() {
    static const ap_xj380_adapter_v1 adapter = {
        AP_XJ380_ADAPTER_VERSION,
        sizeof(ap_xj380_adapter_v1),
        0,
        nullptr, // read_setting
        nullptr, // write_setting
        nullptr, // write_shortcut
        nullptr, // shortcut_extension, for example ".xlink"
        nullptr, // desktop_directory
        nullptr, // launch
        nullptr, // changed
    };
    return &adapter;
}
