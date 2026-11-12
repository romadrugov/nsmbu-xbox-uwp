// Author-created synthetic ELF32 PPC RELA records; no Nintendo game bytes.
#include "core/r5d_relocation_audit.h"
#include <cstdio>
#include <string>
#include <vector>

static void write32(std::vector<uint8_t>& v, size_t at, uint32_t n) {
    v[at] = uint8_t(n >> 24);
    v[at + 1] = uint8_t(n >> 16);
    v[at + 2] = uint8_t(n >> 8);
    v[at + 3] = uint8_t(n);
}
static void write16(std::vector<uint8_t>& v, size_t at, uint16_t n) {
    v[at] = uint8_t(n >> 8);
    v[at + 1] = uint8_t(n);
}
static void rela(std::vector<uint8_t>& file, size_t i,
                 uint32_t off, uint32_t symbol, uint32_t typ) {
    const size_t at = 48u + i * 12u;
    write32(file, at, off);
    write32(file, at + 4u, (symbol << 8u) | typ);
    write32(file, at + 8u, 0u);
}
int main() {
    xboxrpx::Image image{};
    image.machine = 20u;
    image.elf_type = 0xFE01u;
    image.entry = 0x02000000u;
    xboxrpx::Section text{};
    text.name = ".text"; text.type = 1u; text.flags = 6u;
    text.address = image.entry; text.memory_bytes = 32u;
    image.sections.push_back(text); // index 0
    xboxrpx::Section symbols{};
    symbols.name = ".symtab"; symbols.type = 2u;
    symbols.file_offset = 0u; symbols.file_bytes = 48u;
    symbols.memory_bytes = 48u; symbols.entry_bytes = 16u;
    image.sections.push_back(symbols); // index 1
    xboxrpx::Section imp{};
    imp.name = ".fimport_example";
    imp.type = 0x80000002u; imp.address = 0xC0100000u;
    image.sections.push_back(imp); // index 2
    xboxrpx::Section rel{};
    rel.name = ".rela.text"; rel.type = 4u;
    rel.link = 1u; rel.info = 0u; rel.entry_bytes = 12u;
    rel.file_offset = 48u; rel.file_bytes = 60u; rel.memory_bytes = 60u;
    image.sections.push_back(rel); // index 3
    std::vector<uint8_t> file(108u, 0u);
    write16(file, 16u + 14u, 2u); // symbol 1 lives in import section
    write16(file, 32u + 14u, 0u); // symbol 2 is not imported
    rela(file, 0, 0x02000004u, 1u, 6u); // imported .text immediate
    rela(file, 1, 0x02000008u, 1u, 10u); // imported .text branch
    rela(file, 2, 0x0200000cu, 2u, 1u); // normal relocation
    rela(file, 3, 0x02000040u, 2u, 255u); // outside target section
    rela(file, 4, 0x02000010u, 5u, 6u); // invalid symbol index

    xboxrpx::PreparedSection bss{};
    bss.address = 0x101C9700u;
    bss.bytes.resize(391684u);
    xboxrpx::PreparedSection ro{};
    ro.address = 0x10000000u;
    ro.bytes.resize(0x1A6200u);
    std::vector<xboxrpx::PreparedSection> staged = {bss, ro};
    xboxrpx::R5DRelocAudit r{};
    std::string error;
    const auto audit = [&]() {
        return xboxrpx::r5d_audit_relocations(
            file.data(), file.size(), image, staged,
            0x10231120u, 0x10008000u, r, error);
    };
    if (!audit() || !error.empty() ||
        r.rela_sections != 1 || r.records != 5 ||
        r.imported_symbols != 2 || r.text_immediate_candidates != 1 ||
        r.imported_branch_candidates != 1 || r.invalid_symbols != 1 ||
        r.target_outside_section != 1 ||
        r.entry_window_references != 5 ||
        r.r13_window_staged_bytes != 0 || r.r2_window_staged_bytes != 65536 ||
        r.slices.size() != 1 || r.type_counts[6] != 2 ||
        r.type_counts[10] != 1 || r.type_counts[255] != 1) return 1;

    // On failure neither prior report nor caller's input may be mutated.
    const uint64_t prior = r.records;
    image.sections[3].link = 999u;
    if (audit() || r.records != prior || error.empty()) return 2;
    image.sections[3].link = 1u;
    image.sections[3].entry_bytes = 8u;
    if (audit() || r.records != prior) return 3;
    image.sections[3].entry_bytes = 12u;
    image.sections[3].file_bytes = 59u;
    image.sections[3].memory_bytes = 59u;
    if (audit() || r.records != prior) return 4;
    image.sections[3].file_bytes = 60u;
    image.sections[3].memory_bytes = 60u;
    if (!audit()) return 5;

    std::puts("R5D synthetic ELF RELA, import and SDA-window audit PASSED");
    return 0;
}
