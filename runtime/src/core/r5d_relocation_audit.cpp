#include "r5d_relocation_audit.h"
#include <algorithm>
#include <limits>

namespace xboxrpx {
namespace {
constexpr uint32_t kShtRela = 4u;
constexpr uint32_t kShtRel = 9u;
constexpr uint32_t kShtSymtab = 2u;
constexpr uint32_t kShtRplImports = 0x80000002u;
constexpr size_t kRelaRecordBytes = 12u;
constexpr size_t kSymbolBytes = 16u;
// The 512K *record* limit was too small for the verified USA_V0 RPX.
// Bound the sum of decoded RELA *bytes* instead: each section is already
// bounded to 32 MiB by decode_section(), and this 96 MiB total budget
// permits three maximum-sized RELA sections without unbounded allocation.
// Sections are decoded/processed one by one and not retained together.
constexpr uint64_t kMaxRelaDecodedBytes = 96ull * 1024ull * 1024ull;

uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24u) |
           (uint32_t(p[1]) << 16u) |
           (uint32_t(p[2]) << 8u) | uint32_t(p[3]);
}
uint16_t be16(const uint8_t* p) {
    return uint16_t((uint16_t(p[0]) << 8u) | p[1]);
}
bool fail(std::string& error, const char* msg) {
    error = msg;
    return false;
}
bool in_section(uint32_t addr, const Section& sec) {
    const uint64_t a = addr, lo = sec.address;
    return a >= lo && a < lo + sec.memory_bytes;
}
bool is_immediate_rewrite(uint32_t type) {
    // These imported .text relocations are already addressed by the
    // PowerPC->C recompiler's _imm_overrides() generation pass.
    return type == 4u || type == 5u || type == 6u;
}
} // namespace

uint64_t r5d_staged_sda_window_bytes(
    uint32_t base, const std::vector<PreparedSection>& prepared) {
    const uint64_t lo = base > 0x8000u ? uint64_t(base) - 0x8000u : 0u;
    const uint64_t hi = std::min<uint64_t>(
        uint64_t(base) + 0x8000u, 0x100000000ull);
    uint64_t total = 0;
    for (const auto& sec : prepared) {
        const uint64_t a = sec.address, b = a + sec.bytes.size();
        const uint64_t left = std::max(lo, a);
        const uint64_t right = std::min(hi, b);
        if (right > left) total += right - left;
    }
    return total;
}

bool r5d_audit_relocations(
    const uint8_t* file, size_t file_size, const Image& image,
    const std::vector<PreparedSection>& staged,
    uint32_t sda, uint32_t sda2,
    R5DRelocAudit& output, std::string& error) {

    if (!file || !file_size || image.machine != 20u || image.sections.size() > 512)
        return fail(error, "R5D requires an inspected ELF32 PowerPC image");
    R5DRelocAudit report{};
    report.r13_window_staged_bytes = r5d_staged_sda_window_bytes(sda, staged);
    report.r2_window_staged_bytes = r5d_staged_sda_window_bytes(sda2, staged);

    size_t cachedSymbolIndex = image.sections.size();
    uint64_t relaDecodedBytes = 0;
    std::vector<uint8_t> symbolTable;
    for (const auto& section : image.sections) {
        if (section.type == kShtRel) {
            ++report.rel_without_addend_sections;
            continue; // unsupported by the original Cafe OS RELA-only parser
        }
        if (section.type != kShtRela) continue;

        ++report.rela_sections;
        if (section.entry_bytes != 0 && section.entry_bytes != kRelaRecordBytes)
            return fail(error, "R5D unexpected ELF RELA entry size");
        if (section.link >= image.sections.size() ||
            image.sections[section.link].type != kShtSymtab)
            return fail(error, "R5D RELA linked symbol table invalid");
        const auto& symSection = image.sections[section.link];
        if (symSection.entry_bytes != 0 && symSection.entry_bytes != kSymbolBytes)
            return fail(error, "R5D ELF symbol entry size invalid");

        std::string decodeError;
        if (cachedSymbolIndex != section.link) {
            std::vector<uint8_t> sym;
            if (!decode_section(file, file_size, symSection, sym, decodeError))
                return fail(error, "R5D linked symbol table decoding failed");
            if (sym.size() % kSymbolBytes != 0)
                return fail(error, "R5D ELF symbols not 16-byte aligned");
            symbolTable.swap(sym);
            cachedSymbolIndex = section.link;
        }

        // Check the inspected decoded size BEFORE decompressing. The total
        // 96-MiB budget also limits CPU time in the bounded record scan.
        if (section.memory_bytes > kMaxRelaDecodedBytes - relaDecodedBytes)
            return fail(error, "R5D decoded RELA bytes exceed 96 MiB audit budget");
        std::vector<uint8_t> rela;
        if (!decode_section(file, file_size, section, rela, decodeError))
            return fail(error, "R5D RELA section decoding failed");
        if (rela.size() % kRelaRecordBytes != 0)
            return fail(error, "R5D RELA payload not a multiple of 12 bytes");
        // The byte limit is independent of file compression ratio.
        if (rela.size() > kMaxRelaDecodedBytes - relaDecodedBytes)
            return fail(error, "R5D decoded RELA bytes exceed 96 MiB audit budget");
        relaDecodedBytes += rela.size();
        const uint64_t count = rela.size() / kRelaRecordBytes;

        R5DRelocSlice slice{};
        slice.name = section.name;
        const Section* target =
            section.info < image.sections.size()
                ? &image.sections[section.info] : nullptr;
        slice.target = target ? target->name : "(invalid-target-index)";
        slice.entries = static_cast<uint32_t>(count);

        for (size_t i = 0; i < count; ++i) {
            const uint8_t* rec = rela.data() + i * kRelaRecordBytes;
            const uint32_t offset = be32(rec);
            const uint32_t info = be32(rec + 4u);
            const uint32_t type = info & 0xffu;
            const uint32_t symbolIndex = info >> 8u;
            ++report.type_counts[type];

            if (!target || !in_section(offset, *target)) {
                ++slice.target_outside_section;
                ++report.target_outside_section;
            }
            if (offset >= image.entry && offset - image.entry < 0x100u) {
                ++slice.entry_window_references;
                ++report.entry_window_references;
            }

            if (symbolIndex >= symbolTable.size() / kSymbolBytes) {
                ++slice.invalid_symbols;
                ++report.invalid_symbols;
                continue;
            }
            const uint8_t* sym = symbolTable.data() + symbolIndex * kSymbolBytes;
            const uint16_t shndx = be16(sym + 14u);
            const bool imported =
                shndx < image.sections.size() &&
                image.sections[shndx].type == kShtRplImports;
            if (!imported) continue;

            ++slice.imported_symbols;
            ++report.imported_symbols;
            if (target && target->name == ".text" && is_immediate_rewrite(type)) {
                ++slice.text_immediate_candidates;
                ++report.text_immediate_candidates;
            }
            if (target && target->name == ".text" && type == 10u) {
                ++slice.imported_branch_candidates;
                ++report.imported_branch_candidates;
            }
        }
        report.records += count;
        report.slices.push_back(std::move(slice));
    }
    output = std::move(report);
    error.clear();
    return true;
}
} // namespace xboxrpx
