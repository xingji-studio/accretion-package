#pragma once
#include "ap/base.hpp"
namespace ap {
struct Sha256 {
    uint32_t state[8];
    uint8_t buffer[64];
    uint64_t bytes = 0;
    size_t used = 0;
    Sha256();
    void update(const void* data, size_t size);
    void finish(uint8_t output[32]);
    Str hex();

  private:
    void block(const uint8_t* data);
};
Str sha256(const void* data, size_t size);
} // namespace ap
