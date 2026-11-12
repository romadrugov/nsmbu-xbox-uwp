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
// Includes bounded standard allocatable .text/.rodata/.data/.bss family;
// R5E opt-in also stages the desktop loader's .syscall and .module_id.
// Strict address-overlap and total-size checks. All sections
// are fully decoded to host memory before any guest page may be committed.
// The caller handles guest VM, CPU permissions and relocation later.
bool prepare_sections(const uint8_t* file, size_t file_size, const Image& image,
                      std::vector<PreparedSection>& result, std::string& error,
                       bool includeBootSections = false);
// Pure metadata helper: bounds the contiguous MEM2 region initialized by
// upstream load_rpx(). Used only for verified owner-local input, no writes.
// Returns false on invalid or unexpectedly large span; no guest allocation.
bool boot_mem2_span(const std::vector<PreparedSection>& sections,
                    uint32_t& first, uint32_t& bytes);
uint32_t crc32(const uint8_t* bytes, size_t count);
} // namespace xboxrpx
