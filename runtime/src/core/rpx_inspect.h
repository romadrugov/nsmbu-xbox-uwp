#pragma once
// RPX/ELF32 PPC structural validation. Metadata only: does NOT execute,
// decompress, relocate, or link any Nintendo content.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xboxrpx {
struct Section {
    std::string name;
    uint32_t type = 0, flags = 0, address = 0;
    uint32_t file_offset = 0, file_bytes = 0, memory_bytes = 0;
    bool compressed = false;
    bool allocated() const { return (flags & 2u) != 0u; }
    bool executable() const { return (flags & 4u) != 0u; }
};
struct Image {
    uint32_t entry = 0;
    std::vector<Section> sections;
    size_t mapped_count = 0;
    size_t compressed_count = 0;
    uint64_t mapped_bytes = 0;
};
bool inspect(const uint8_t* bytes, size_t byte_count,
             Image& output, std::string& error);
const Section* section_named(const Image& image, const char* name);
} // namespace xboxrpx
