#include "ap/engine.hpp"
namespace ap {
// A loaded writable section, retained in each platform's stub and patched without
// changing its code. Launchers and uninstallers need no package or cwd discovery.
#if defined(_MSC_VER)
#pragma section(".apctx", read, write)
__declspec(allocate(".apctx")) volatile ContextBlock embeddedContext = {
#else
__attribute__((used, section(".apctx"))) volatile ContextBlock embeddedContext = {
#endif
    {0x41, 0x50, 0x43, 0x54, 0x58, 0x31, 0x9d, 0xc7, 0x55, 0x3a, 0xe1, 0x68, 0xb2, 0x06, 0xf4, 0x8c},
    0,
    0,
    {0}};

#if defined(_MSC_VER)
#pragma section(".apembed", read, write)
__declspec(allocate(".apembed")) volatile PayloadBlock embeddedPayload = {
#else
__attribute__((used, section(".apembed"))) volatile PayloadBlock embeddedPayload = {
#endif
    {'A', 'P', 'E', 'M', 'B', 'E', 'D', '1', 0xa9, 0xc2, 0x37, 0xe4, 0x6b, 0x8d, 0x50, 0xf1}, 0, 0, 0, {0}};
Json readContext() {
    uint32_t length = embeddedContext.length;
    if (!length || length > sizeof(embeddedContext.json))
        return {};
    Str text;
    for (uint32_t i = 0; i < length; ++i)
        text.push(embeddedContext.json[i]);
    Json context;
    if (!parseJson(text, context))
        return {};
    return context;
}
Status makeHelper(const Str& source, uint64_t stubSize, const Str& output, const Json& context) {
    auto stat = os::info(source);
    uint64_t size = stubSize ? stubSize : stat.size;
    if (!stat.exists || size > 64 * 1024 * 1024 || size > stat.size)
        return Status::fail("Runtime stub is missing or too large");
    Str data;
    data.resize(size_t(size));
    size_t got = 0;
    AP_TRY(os::read(source, 0, data.data(), data.size(), got));
    if (got != data.size())
        return Status::fail("Cannot read runtime stub");
    static const uint8_t encoded[] = {0x1b, 0x0a, 0x19, 0x0e, 0x02, 0x6b, 0xc7, 0x9d,
                                      0x0f, 0x60, 0xbb, 0x32, 0xe8, 0x5c, 0xae, 0xd6};
    volatile uint8_t salt = 0x5a;
    Str marker;
    for (uint8_t byte : encoded)
        marker.push(char(byte ^ salt));
    size_t at = data.find(marker);
    if (at == size_t(-1) || at + sizeof(ContextBlock) > data.size() ||
        data.find(marker, at + 1) != size_t(-1))
        return Status::fail("Invalid runtime context slot");
    Str json = context.dump();
    if (json.size() > sizeof(embeddedContext.json))
        return Status::fail("Launcher context exceeds 16 KiB");
    uint8_t* slot = reinterpret_cast<uint8_t*>(data.data() + at);
    write32(slot + 16, uint32_t(json.size()));
    write32(slot + 20, 0);
    ap_memset(slot + 24, 0, sizeof(embeddedContext.json));
    ap_memcpy(slot + 24, json.c_str(), json.size());
    AP_TRY(os::writeText(output, data));
    return os::chmod(output, 0755);
}
} // namespace ap
