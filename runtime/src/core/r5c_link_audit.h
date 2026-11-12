#pragma once
// Game-byte-free audit of RPX section coverage. Does NOT relocate, allocate,
// execute PPC or infer Cafe OS behavior from a non-null native import pointer.
#include "rpx_stage.h"
#include <cstdint>
#include <vector>
namespace xboxrpx {
struct R5CLinkAudit {
    uint32_t allocated_sections = 0;
    uint32_t staged_sections = 0;
    uint32_t unstaged_allocated = 0;
    uint64_t unstaged_allocated_bytes = 0;
    uint32_t relocation_candidate_sections = 0;
    uint64_t relocation_candidate_bytes = 0;
    bool entry_staged = false;
    bool sda_staged = false;
    bool sda2_staged = false;
};
bool r5c_is_staged(uint32_t addr, const std::vector<PreparedSection>& prepared);
bool r5c_section_staged(const Section& source, const std::vector<PreparedSection>& prepared);
R5CLinkAudit r5c_audit(const Image& image, const std::vector<PreparedSection>& prepared,
                       uint32_t sda, uint32_t sda2);
}
