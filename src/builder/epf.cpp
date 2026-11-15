#include "epf.hpp"
#include "ap/package.hpp"
#include <elf2epf/converter.hpp>
#include <stdexcept>
using namespace ap;
namespace {
uint16_t u16(const uint8_t* p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
uint64_t aligned(uint64_t value) {
    return (value + 4095) & ~uint64_t(4095);
}
Str marker() {
    return Str("APEMBED1\xa9\xc2\x37\xe4\x6b\x8d\x50\xf1", 16);
}
void digest(const Str& value, uint8_t* output) {
    Sha256 hash;
    hash.update(value.c_str(), value.size());
    hash.finish(output);
}
} // namespace
Str xj380Epf(Str stub, const Str& archive) {
    if (stub.size() < 64 || stub.substr(0, 4) != Str("\x7f"
                                                     "ELF",
                                                     4))
        throw std::runtime_error("EPF output needs a native XJ380 ELF stub");
    auto* data = reinterpret_cast<uint8_t*>(stub.data());
    if (data[4] != 2 || data[5] != 1 || u16(data + 16) != 2 || u16(data + 18) != 62)
        throw std::runtime_error("Expected x86-64 fixed-address ELF");
    uint64_t phoff = read64(data + 32);
    uint16_t stride = u16(data + 54), count = u16(data + 56);
    if (stride != 56 || phoff > stub.size() || uint64_t(count) * stride > stub.size() - phoff)
        throw std::runtime_error("Invalid ELF program headers");
    size_t slot = stub.find(marker());
    if (slot == size_t(-1) || slot + 72 > stub.size() || stub.find(marker(), slot + 1) != size_t(-1))
        throw std::runtime_error("Missing native embedded-payload descriptor");
    size_t program = size_t(-1);
    uint64_t top = 0;
    for (unsigned i = 0; i < count; ++i) {
        size_t at = size_t(phoff) + i * stride;
        uint32_t type = read32(data + at);
        if (type == 0)
            program = at;
        if (type == 1) {
            uint64_t address = read64(data + at + 16), memory = read64(data + at + 40);
            if (address > UINT64_MAX - memory)
                throw std::runtime_error("ELF image overflow");
            if (address + memory > top)
                top = address + memory;
        }
    }
    if (program == size_t(-1))
        throw std::runtime_error("Runtime has no reserved payload program header; rebuild the XJ380 stubs");
    uint64_t original = stub.size(), offset = aligned(original), address = aligned(top);
    if (offset > 256 * 1024 * 1024 || archive.size() > 256 * 1024 * 1024 - offset)
        throw std::runtime_error(
            "Single-file EPF is limited to 256 MiB; use ELF or a bundle for a larger application");
    stub.resize(size_t(offset));
    stub.append(archive);
    data = reinterpret_cast<uint8_t*>(stub.data());
    write32(data + program, 1);
    write32(data + program + 4, 4);
    write64(data + program + 8, offset);
    write64(data + program + 16, address);
    write64(data + program + 24, address);
    write64(data + program + 32, archive.size());
    write64(data + program + 40, archive.size());
    write64(data + program + 48, 4096);
    write64(data + slot + 16, address);
    write64(data + slot + 24, archive.size());
    write64(data + slot + 32, original);
    digest(archive, data + slot + 40);
    epf::ConvertOptions options;
    options.profile.os_abi = 0x0380;
    options.profile.abi_version = 1;
    options.profile.map_align = 4096;
    auto image = epf::convert(epf::Bytes(data, data + stub.size()), options);
    epf::decode(image.bytes);
    return Str(reinterpret_cast<const char*>(image.bytes.data()), image.bytes.size());
}
Json inspectEpfPackage(const Str& binary) {
    auto image = epf::decode(epf::Bytes(binary.c_str(), binary.c_str() + binary.size()));
    if (image.header.os_abi != 0x0380 || image.header.abi_version != 1)
        throw std::runtime_error("Not an XJ380 ABI 1 installer");
    for (const auto& segment : image.segments) {
        if (segment.type != epf::SegmentType::load || !(segment.flags & epf::writable))
            continue;
        Str bytes(binary.c_str() + segment.file_offset, size_t(segment.file_size));
        size_t at = bytes.find(marker());
        if (at == size_t(-1) || at + 72 > bytes.size())
            continue;
        const auto* data = reinterpret_cast<const uint8_t*>(bytes.c_str() + at);
        uint64_t address = read64(data + 16), size = read64(data + 24);
        for (const auto& payload : image.segments) {
            uint64_t start = image.header.image_base + payload.rva;
            if (payload.type != epf::SegmentType::load || address < start ||
                address - start > payload.file_size || size > payload.file_size - (address - start))
                continue;
            uint64_t offset = payload.file_offset + (address - start);
            if (size > binary.size() - offset)
                continue;
            uint8_t expected[32];
            Str archive(binary.c_str() + offset, size_t(size));
            digest(archive, expected);
            bool valid = true;
            for (unsigned i = 0; i < 32; ++i)
                if (expected[i] != data[40 + i])
                    valid = false;
            if (!valid)
                throw std::runtime_error("Embedded EPF payload integrity check failed");
            Package package;
            Status status =
                package.fromMemory(reinterpret_cast<const uint8_t*>(archive.c_str()), archive.size());
            if (!status)
                throw std::runtime_error(status.message.c_str());
            return package.manifest;
        }
    }
    throw std::runtime_error("No Accretion payload found in EPF");
}
