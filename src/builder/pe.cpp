#include "pe.hpp"
#include "ap/package.hpp"
#include <map>
#include <stdexcept>
#include <vector>
using namespace ap;
namespace {
using Bytes = std::vector<uint8_t>;
struct Image {
    uint8_t width, height, colors;
    uint16_t planes, bits;
    Bytes bytes;
};
void put16(Bytes& data, size_t at, uint16_t value) {
    data[at] = uint8_t(value);
    data[at + 1] = uint8_t(value >> 8);
}
void put32(Bytes& data, size_t at, uint32_t value) {
    write32(data.data() + at, value);
}
uint16_t get16(const uint8_t* p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
size_t allocate(Bytes& bytes, size_t count) {
    size_t at = bytes.size();
    bytes.resize(at + count);
    return at;
}
size_t align(Bytes& bytes) {
    while (bytes.size() % 4)
        bytes.push_back(0);
    return bytes.size();
}
std::vector<Image> images(const Str& icon) {
    std::vector<Image> result;
    if (icon.empty()) {
        Image image{32, 32, 0, 1, 32, Bytes(40 + 32 * 32 * 4 + 32 * 4)};
        put32(image.bytes, 0, 40);
        put32(image.bytes, 4, 32);
        put32(image.bytes, 8, 64);
        put16(image.bytes, 12, 1);
        put16(image.bytes, 14, 32);
        put32(image.bytes, 20, 32 * 32 * 4);
        for (unsigned y = 0; y < 32; ++y)
            for (unsigned x = 0; x < 32; ++x) {
                bool letter = (y >= 7 && y <= 25) && ((x >= 11 && x <= 14) || (x >= 19 && x <= 22) ||
                                                      (y >= 7 && y <= 10 && x >= 11 && x <= 22) ||
                                                      (y >= 16 && y <= 18 && x >= 11 && x <= 22));
                size_t at = 40 + ((31 - y) * 32 + x) * 4;
                image.bytes[at] = letter ? 255 : 205;
                image.bytes[at + 1] = letter ? 255 : 88;
                image.bytes[at + 2] = letter ? 255 : 40;
                image.bytes[at + 3] = 255;
            }
        result.push_back(std::move(image));
        return result;
    }
    const auto* data = reinterpret_cast<const uint8_t*>(icon.c_str());
    if (icon.size() >= 24 && data[0] == 0x89 && icon.substr(1, 7) == Str("PNG\r\n\x1a\n", 7)) {
        auto be = [&](size_t at) {
            return uint32_t(data[at]) << 24 | uint32_t(data[at + 1]) << 16 | uint32_t(data[at + 2]) << 8 |
                   data[at + 3];
        };
        uint32_t width = be(16), height = be(20);
        if (!width || !height || width > 256 || height > 256)
            throw std::runtime_error(
                "Windows PNG icons must be at most 256 x 256; use a suitable ICO or PNG");
        result.push_back({uint8_t(width == 256 ? 0 : width), uint8_t(height == 256 ? 0 : height), 0, 1, 32,
                          Bytes(data, data + icon.size())});
        return result;
    }
    if (icon.size() < 6 || get16(data) != 0 || get16(data + 2) != 1)
        throw std::runtime_error("Windows installerIcon must be ICO or PNG");
    unsigned count = get16(data + 4);
    if (!count || count > 64 || icon.size() < 6 + size_t(count) * 16)
        throw std::runtime_error("Invalid ICO directory");
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t* entry = data + 6 + 16 * i;
        uint32_t size = read32(entry + 8), offset = read32(entry + 12);
        if (!size || offset > icon.size() || size > icon.size() - offset)
            throw std::runtime_error("Invalid ICO image bounds");
        result.push_back({entry[0], entry[1], entry[2], get16(entry + 4), get16(entry + 6),
                          Bytes(data + offset, data + offset + size)});
    }
    return result;
}
Bytes resources(const std::vector<Image>& icons, uint32_t rva) {
    std::map<uint32_t, std::map<uint32_t, Bytes>> entries;
    for (size_t i = 0; i < icons.size(); ++i)
        entries[3][uint32_t(i + 1)] = icons[i].bytes;
    Bytes group(6 + 14 * icons.size());
    put16(group, 2, 1);
    put16(group, 4, uint16_t(icons.size()));
    for (size_t i = 0; i < icons.size(); ++i) {
        size_t at = 6 + 14 * i;
        group[at] = icons[i].width;
        group[at + 1] = icons[i].height;
        group[at + 2] = icons[i].colors;
        put16(group, at + 4, icons[i].planes);
        put16(group, at + 6, icons[i].bits);
        put32(group, at + 8, uint32_t(icons[i].bytes.size()));
        put16(group, at + 12, uint16_t(i + 1));
    }
    entries[14][1] = std::move(group);
    const char manifest[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><assembly "
        "xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\"><assemblyIdentity "
        "version=\"1.0.0.0\" processorArchitecture=\"amd64\" name=\"Accretion.Installer\" "
        "type=\"win32\"/><trustInfo "
        "xmlns=\"urn:schemas-microsoft-com:asm.v3\"><security><requestedPrivileges><requestedExecutionLevel "
        "level=\"asInvoker\" uiAccess=\"false\"/></requestedPrivileges></security></trustInfo><application "
        "xmlns=\"urn:schemas-microsoft-com:asm.v3\"><windowsSettings><dpiAware "
        "xmlns=\"http://schemas.microsoft.com/SMI/2005/WindowsSettings\">true</dpiAware><longPathAware "
        "xmlns=\"http://schemas.microsoft.com/SMI/2016/WindowsSettings\">true</longPathAware></"
        "windowsSettings></application></assembly>";
    entries[24][1] = Bytes(manifest, manifest + sizeof(manifest) - 1);
    Bytes output(16 + 8 * entries.size());
    put16(output, 14, uint16_t(entries.size()));
    size_t typeIndex = 0;
    for (const auto& type : entries) {
        size_t typeDirectory = allocate(output, 16 + 8 * type.second.size());
        put32(output, 16 + typeIndex * 8, type.first);
        put32(output, 20 + typeIndex * 8, 0x80000000U | uint32_t(typeDirectory));
        put16(output, typeDirectory + 14, uint16_t(type.second.size()));
        size_t idIndex = 0;
        for (const auto& id : type.second) {
            size_t language = allocate(output, 24), dataEntry = allocate(output, 16);
            put32(output, typeDirectory + 16 + idIndex * 8, id.first);
            put32(output, typeDirectory + 20 + idIndex * 8, 0x80000000U | uint32_t(language));
            put16(output, language + 14, 1);
            put32(output, language + 16, 1033);
            put32(output, language + 20, uint32_t(dataEntry));
            size_t data = align(output);
            output.insert(output.end(), id.second.begin(), id.second.end());
            put32(output, dataEntry, rva + uint32_t(data));
            put32(output, dataEntry + 4, uint32_t(id.second.size()));
            ++idIndex;
        }
        ++typeIndex;
    }
    return output;
}
} // namespace
Str windowsResources(Str stub, const Str& icon) {
    if (stub.size() < 256)
        throw std::runtime_error("Invalid Windows stub");
    auto* data = reinterpret_cast<uint8_t*>(stub.data());
    uint32_t pe = read32(data + 0x3c);
    if (pe > stub.size() - 256 || stub.substr(pe, 4) != Str("PE\0\0", 4))
        throw std::runtime_error("Windows stub is not PE");
    uint16_t sections = get16(data + pe + 6), optionalSize = get16(data + pe + 20);
    size_t optional = pe + 24, sectionTable = optional + optionalSize;
    if (optionalSize < 136 || get16(data + optional) != 0x20b ||
        sectionTable + size_t(sections) * 40 > stub.size())
        throw std::runtime_error("Expected a PE32+ stub");
    Str marker("APICONRESERVE1\xa7\xd2\x6e", 17);
    size_t slot = stub.find(marker);
    if (slot == size_t(-1) || slot + 262144 > stub.size())
        throw std::runtime_error("Windows stub lacks its icon resource slot; rebuild the runtime");
    uint32_t rva = 0;
    for (unsigned i = 0; i < sections; ++i) {
        const uint8_t* section = data + sectionTable + i * 40;
        uint32_t raw = read32(section + 20), size = read32(section + 16);
        if (slot >= raw && slot - raw + 262144 <= size)
            rva = read32(section + 12) + uint32_t(slot - raw);
    }
    if (!rva)
        throw std::runtime_error("Icon resource slot is outside PE sections");
    Bytes resource = resources(images(icon), rva);
    if (resource.size() > 262144)
        throw std::runtime_error("Icon resources exceed 256 KiB");
    ap_memset(data + slot, 0, 262144);
    ap_memcpy(data + slot, resource.data(), resource.size());
    write32(data + optional + 112 + 16, rva);
    write32(data + optional + 112 + 20, uint32_t(resource.size()));
    write32(data + optional + 64, 0);
    return stub;
}
