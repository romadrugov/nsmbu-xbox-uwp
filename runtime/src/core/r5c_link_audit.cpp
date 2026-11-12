#include "r5c_link_audit.h"
#include <string>
namespace xboxrpx {
bool r5c_is_staged(uint32_t addr, const std::vector<PreparedSection>& prepared) {
    for (const auto& p : prepared) {
        const uint64_t begin = p.address;
        const uint64_t end = begin + p.bytes.size();
        if (addr >= begin && uint64_t(addr) < end) return true;
    }
    return false;
}
bool r5c_section_staged(const Section& source, const std::vector<PreparedSection>& prepared) {
    for (const auto& p : prepared) {
        if (p.name == source.name && p.address == source.address &&
            p.bytes.size() == source.memory_bytes) return true;
    }
    return false;
}
R5CLinkAudit r5c_audit(const Image& image, const std::vector<PreparedSection>& prepared,
                       uint32_t sda, uint32_t sda2) {
    R5CLinkAudit a{};
    for (const auto& sec : image.sections) {
        if (sec.name.compare(0, 4, ".rel") == 0 || sec.type == 4u || sec.type == 9u) {
            ++a.relocation_candidate_sections;
            a.relocation_candidate_bytes += sec.file_bytes;
        }
        if (!sec.allocated() || !sec.memory_bytes) continue;
        ++a.allocated_sections;
        if (r5c_section_staged(sec, prepared)) ++a.staged_sections;
        else {
            ++a.unstaged_allocated;
            a.unstaged_allocated_bytes += sec.memory_bytes;
        }
    }
    a.entry_staged = r5c_is_staged(image.entry, prepared);
    a.sda_staged = r5c_is_staged(sda, prepared);
    a.sda2_staged = r5c_is_staged(sda2, prepared);
    return a;
}
} // namespace xboxrpx
