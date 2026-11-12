#pragma once
// Public, game-byte-free startup metadata parser for a previously inspected
// Cafe OS RPX. The caller must independently verify the owner's RPX SHA256.
#include "rpx_inspect.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace xboxrpx {
struct StartupMetadata {
    uint32_t entry = 0;
    uint32_t sda_base = 0;
    uint32_t sda2_base = 0;
    uint32_t stack_requested = 0;
    uint32_t stack_reserved = 0;
};
// Extracts the Wii U .dinfo-style module metadata (section type 0x80000004)
// used by the original runtime loader. Does not modify guest memory or run PPC.
// Bounds and alignment constraints are deliberately conservative for R5B.
bool read_startup_metadata(const uint8_t* file, size_t length,
                           const Image& inspected,
                           StartupMetadata& output,
                           std::string& error);
}
