#pragma once
// Xbox R5D: read-only Cafe OS ELF32/PowerPC relocation inventory.
// Parses owner-local, SHA-verified RPX; never applies a relocation, invokes
// generated game code, creates HLE services, or uploads proprietary bytes.
#include "rpx_stage.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xboxrpx {
struct R5DRelocSlice {
    std::string name, target;
    uint32_t entries = 0;
    uint32_t imported_symbols = 0;
    uint32_t text_immediate_candidates = 0;
    uint32_t imported_branch_candidates = 0;
    uint32_t entry_window_references = 0;
    uint32_t target_outside_section = 0;
    uint32_t invalid_symbols = 0;
};
struct R5DRelocAudit {
    uint32_t rela_sections = 0;
    uint32_t rel_without_addend_sections = 0;
    uint64_t records = 0;
    uint64_t imported_symbols = 0;
    uint64_t text_immediate_candidates = 0;
    uint64_t imported_branch_candidates = 0;
    uint64_t entry_window_references = 0;
    uint64_t target_outside_section = 0;
    uint64_t invalid_symbols = 0;
    uint64_t r13_window_staged_bytes = 0;
    uint64_t r2_window_staged_bytes = 0;
    std::array<uint64_t, 256> type_counts{};
    std::vector<R5DRelocSlice> slices;
};
// Quantifies which bytes intersect the signed-16-bit SDA-relative address
// window [base-0x8000, base+0x7fff]. Not a guest access/permissions test.
uint64_t r5d_staged_sda_window_bytes(
    uint32_t base, const std::vector<PreparedSection>& prepared);
// Limits: 512K RELA records total; one decoded symbol table <=32MiB;
// all section offsets/symbol indices bounded. Output unchanged on failure.
// R5D is a metadata-only audit: already-recompiled import address rewrites
// may be present in native generated code and must NOT be re-applied.
bool r5d_audit_relocations(
    const uint8_t* file, size_t file_size, const Image& image,
    const std::vector<PreparedSection>& staged,
    uint32_t sda, uint32_t sda2,
    R5DRelocAudit& output, std::string& error);
} // namespace xboxrpx
