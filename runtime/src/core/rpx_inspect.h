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
    // ELF header properties are reported even for a Cafe OS executable.
    uint16_t elf_type = 0;        // ET_EXEC=2, Cafe OS RPX/RPL=0xFE01
    uint16_t machine = 0;         // EM_PPC=20
    uint8_t os_abi = 0;           // ELF SYSV=0 or Cafe OS=0xCA
    uint32_t entry = 0;
    std::vector<Section> sections;
    size_t mapped_count = 0;
    size_t compressed_count = 0;
    uint64_t mapped_bytes = 0;
    bool section_names_compressed = false;
    size_t section_names_decoded_bytes = 0;
};
bool inspect(const uint8_t* bytes, size_t byte_count,
             Image& output, std::string& error);
const Section* section_named(const Image& image, const char* name);

// Decode exactly one previously inspected section, without modifying guest
// memory. Handles Cafe OS big-endian-size-prefixed zlib or NOBITS zero fill.
// Enforces a per-section limit of 32 MiB; leaves output unchanged on failure.
bool decode_section(const uint8_t* file, size_t file_size, const Section& section,
                    std::vector<uint8_t>& output, std::string& error);
} // namespace xboxrpx
