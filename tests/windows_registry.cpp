#include "ap/platform.hpp"
#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <windows.h>
using namespace ap;
int main() {
    Str path = "Software\\AccretionPackageTests\\" + number(os::nonce());
    List<Json> descriptors, previous;
    bool failed = false;
    const char* types[] = {"string", "expandString", "dword"};
    for (const char* type : types) {
        Json descriptor = Json::object();
        descriptor.set("hive", Json("HKCU"));
        descriptor.set("key", Json(path));
        descriptor.set("name", Json(type));
        Json old;
        Status read = os::registryRead(descriptor, old);
        if (!read) {
            failed = true;
            break;
        }
        descriptors.add(descriptor);
        previous.add(old);
        Json expected = Json::object();
        expected.set("exists", Json::boolean(true));
        expected.set("type", Json(type));
        expected.set("value",
                     equal(type, "dword") ? Json::integer(123456) : Json("Unicode 测试 %USERPROFILE%"));
        Status written = os::registryWrite(descriptor, expected);
        Json actual;
        Status reread = os::registryRead(descriptor, actual);
        if (!written || !reread || actual.dump() != expected.dump())
            failed = true;
    }
    for (size_t i = 0; i < descriptors.size(); ++i)
        if (!os::registryWrite(descriptors[i], previous[i]))
            failed = true;
    // Remove only our unique test key; never modify the user's environment values.
    int count = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    auto* wide = static_cast<wchar_t*>(ap_allocate(size_t(count) * sizeof(wchar_t)));
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide, count);
    RegDeleteKeyW(HKEY_CURRENT_USER, wide);
    ap_deallocate(wide);
    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\AccretionPackageTests");
    puts(failed ? "Windows registry checks FAILED" : "Windows registry checks passed");
    return failed ? 1 : 0;
}
