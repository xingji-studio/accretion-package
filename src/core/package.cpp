#include "ap/package.hpp"
#include "lz4.h"
namespace ap {
uint32_t read32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint64_t read64(const uint8_t* p) {
    return uint64_t(read32(p)) | (uint64_t(read32(p + 4)) << 32);
}
void write32(uint8_t* p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = uint8_t(v >> (8 * i));
}
void write64(uint8_t* p, uint64_t v) {
    for (unsigned i = 0; i < 8; ++i)
        p[i] = uint8_t(v >> (8 * i));
}
namespace {
bool magic(const uint8_t* p, const char* s) {
    for (unsigned i = 0; i < 8; ++i)
        if (p[i] != uint8_t(s[i]))
            return false;
    return true;
}
} // namespace
Status Package::readAt(uint64_t relative, void* output, size_t length) const {
    if (relative > size || length > size - relative)
        return Status::fail("Package read outside bounds");
    if (memory) {
        ap_memcpy(output, memory + relative, length);
        return {};
    }
    size_t actual = 0;
    AP_TRY(os::read(path, offset + relative, output, length, actual));
    return actual == length ? Status{} : Status::fail("Truncated package");
}
Status Package::open(const Str& filename) {
    path = filename;
    memory = nullptr;
    auto stat = os::info(path);
    if (!stat.exists || stat.directory || stat.link || stat.size < 64)
        return Status::fail("Not a package: " + path);
    uint8_t header[64];
    size_t got = 0;
    AP_TRY(os::read(path, 0, header, 64, got));
    if (got != 64)
        return Status::fail("Truncated package");
    offset = 0;
    size = stat.size;
    stubSize = 0;
    if (!magic(header, "ACPKG001")) {
        uint8_t footer[64];
        AP_TRY(os::read(path, stat.size - 64, footer, 64, got));
        if (got != 64 || !magic(footer, "ACFTR001"))
            return Status::fail("No embedded Accretion package");
        offset = read64(footer + 8);
        size = read64(footer + 16);
        stubSize = read64(footer + 56);
        if (offset > stat.size - 64 || size != stat.size - 64 - offset || stubSize > offset || !size)
            return Status::fail("Invalid package footer");
        Sha256 hash;
        Str block;
        block.resize(65536);
        for (uint64_t pos = 0; pos < size;) {
            size_t n = size_t(size - pos);
            if (n > block.size())
                n = block.size();
            AP_TRY(readAt(pos, block.data(), n));
            hash.update(block.c_str(), n);
            pos += n;
        }
        uint8_t digest[32];
        hash.finish(digest);
        for (unsigned i = 0; i < 32; ++i)
            if (digest[i] != footer[24 + i])
                return Status::fail("Package integrity check failed");
    }
    return load();
}
Status Package::fromMemory(const uint8_t* data, size_t length) {
    memory = data;
    offset = 0;
    size = length;
    return load();
}
Status Package::load() {
    uint8_t header[64];
    AP_TRY(readAt(0, header, 64));
    if (!magic(header, "ACPKG001") || read32(header + 8) != 1 || read32(header + 12) != 64)
        return Status::fail("Unsupported package version");
    uint64_t length = read64(header + 16);
    payloadSize = read64(header + 24);
    if (length > 16 * 1024 * 1024 || length > size - 64 || payloadSize != size - 64 - length)
        return Status::fail("Invalid manifest or payload size");
    Str text;
    text.resize(size_t(length));
    AP_TRY(readAt(64, text.data(), text.size()));
    Sha256 hash;
    hash.update(text.c_str(), text.size());
    uint8_t digest[32];
    hash.finish(digest);
    for (unsigned i = 0; i < 32; ++i)
        if (digest[i] != header[32 + i])
            return Status::fail("Manifest integrity check failed");
    AP_TRY(parseJson(text, manifest));
    AP_TRY(validateManifest(manifest, true));
    payload = 64 + length;
    for (const auto& file : manifest.get("files").values)
        for (const auto& chunk : file.get("chunks").values) {
            uint64_t at = chunk.integer("offset", UINT64_MAX), stored = chunk.integer("stored", UINT64_MAX);
            if (at > payloadSize || stored > payloadSize - at)
                return Status::fail("Chunk outside payload");
        }
    return {};
}
Status Package::extract(const Json& file, const Str& destination) const {
    AP_TRY(os::directories(parent(destination)));
    AP_TRY(os::write(destination, 0, "", 0, true));
    // XJ380 applications have a small native stack. Keep streaming buffers on the heap.
    Str compressed, raw;
    compressed.resize(ChunkSize + 2048);
    raw.resize(ChunkSize);
    uint64_t written = 0;
    Sha256 hash;
    for (const auto& chunk : file.get("chunks").values) {
        uint64_t stored = chunk.integer("stored", UINT64_MAX), bytes = chunk.integer("size", UINT64_MAX),
                 at = chunk.integer("offset", UINT64_MAX);
        if (!bytes || bytes > ChunkSize || !stored || stored > compressed.size())
            return Status::fail("Invalid chunk size");
        AP_TRY(readAt(payload + at, compressed.data(), size_t(stored)));
        Str codec = chunk.str("codec");
        if (codec == "store") {
            if (bytes != stored)
                return Status::fail("Stored chunk length mismatch");
            ap_memcpy(raw.data(), compressed.c_str(), size_t(bytes));
        } else if (codec == "lz4") {
            int actual = LZ4_decompress_safe(compressed.c_str(), raw.data(), int(stored), int(bytes));
            if (actual != int(bytes))
                return Status::fail("Invalid compressed block");
        } else
            return Status::fail("Unsupported chunk codec");
        if (written > file.integer("size", 0) || bytes > file.integer("size", 0) - written)
            return Status::fail("File size overflow");
        AP_TRY(os::write(destination, written, raw.c_str(), size_t(bytes)));
        hash.update(raw.c_str(), size_t(bytes));
        written += bytes;
    }
    if (written != file.integer("size", UINT64_MAX) || hash.hex() != file.str("sha256"))
        return Status::fail("File integrity check failed: " + file.str("path"));
    return os::chmod(destination, uint32_t(file.integer("mode", 0644)));
}
Status fileHash(const Str& path, Str& digest) {
    auto stat = os::info(path);
    if (!stat.exists || stat.directory || stat.link)
        return Status::fail("Cannot hash regular file: " + path);
    Str buffer;
    buffer.resize(65536);
    Sha256 hash;
    uint64_t pos = 0;
    while (pos < stat.size) {
        size_t n = size_t(stat.size - pos);
        if (n > buffer.size())
            n = buffer.size();
        size_t got = 0;
        AP_TRY(os::read(path, pos, buffer.data(), n, got));
        if (!got)
            return Status::fail("File changed while hashing: " + path);
        hash.update(buffer.c_str(), got);
        pos += got;
    }
    digest = hash.hex();
    return {};
}
} // namespace ap
