#pragma once
// Headless data-only staging of local RPX segments; never executes PPC code.
#include "rpx_inspect.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace xboxrpx {
struct PreparedSection {
    std::string name;
    uint32_t address = 0;
    bool zero_fill = false;
    uint32_t crc32 = 0;
    std::vector<uint8_t> bytes;
};
// Includes only the bounded standard allocatable .text/.rodata/.data/.bss
// family, with strict address-overlap and total-size checks. All sections
// are fully decoded to host memory before any guest page may be committed.
// The caller handles guest VM, CPU permissions and relocation later.
bool prepare_sections(const uint8_t* file, size_t file_size, const Image& image,
                      std::vector<PreparedSection>& result, std::string& error);
uint32_t crc32(const uint8_t* bytes, size_t count);
} // namespace xboxrpx
