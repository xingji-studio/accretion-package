#pragma once
#include "ap/hash.hpp"
#include "ap/platform.hpp"
namespace ap {
constexpr uint64_t PackageHeaderSize = 64, PackageFooterSize = 64;
constexpr uint32_t ChunkSize = 262144;
uint32_t read32(const uint8_t* p);
uint64_t read64(const uint8_t* p);
void write32(uint8_t* p, uint32_t value);
void write64(uint8_t* p, uint64_t value);
struct Package {
    Str path;
    Json manifest;
    uint64_t offset = 0, size = 0, payload = 0, payloadSize = 0, stubSize = 0;
    const uint8_t* memory = nullptr;
    Status open(const Str& path);
    Status fromMemory(const uint8_t* data, size_t length);
    Status readAt(uint64_t relative, void* output, size_t length) const;
    Status extract(const Json& file, const Str& destination) const;

  private:
    Status load();
};
Status fileHash(const Str& path, Str& digest);
Status validateManifest(const Json& manifest, bool packaged = false);
Str targetExecutable(const Json& manifest, const Str& id);
bool validId(const Str& id);
bool validVariable(const Str& name);
Str expand(const Str& text, const Json& manifest, const Str& root, bool machine);
} // namespace ap
