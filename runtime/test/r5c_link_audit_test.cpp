// Entirely hand-authored synthetic memory layout; no Nintendo RPX.
#include "core/r5c_link_audit.h"
#include <cstdio>
int main() {
    xboxrpx::Image i{};
    i.entry = 0x02000000u;
    xboxrpx::Section text{};
    text.name=".text"; text.type=1u; text.flags=6u;
    text.address=0x02000000u; text.memory_bytes=12u;
    i.sections.push_back(text);
    xboxrpx::Section data{};
    data.name=".data"; data.type=1u; data.flags=3u;
    data.address=0x10000000u; data.memory_bytes=16u;
    i.sections.push_back(data);
    xboxrpx::Section extra{};
    extra.name=".sdata3"; extra.type=1u; extra.flags=3u;
    extra.address=0x10008000u; extra.memory_bytes=32u;
    i.sections.push_back(extra);
    xboxrpx::Section rel{};
    rel.name=".rela.text"; rel.type=4u; rel.file_bytes=36u; rel.flags=0u;
    i.sections.push_back(rel);
    xboxrpx::PreparedSection t{};
    t.name=".text";t.address=0x02000000u;t.bytes.resize(12);
    xboxrpx::PreparedSection d{};
    d.name=".data";d.address=0x10000000u;d.bytes.resize(16);
    std::vector<xboxrpx::PreparedSection> prepared={t,d};
    const auto report=xboxrpx::r5c_audit(i,prepared,0x10008010u,0x10000004u);
    if (report.allocated_sections!=3 || report.staged_sections!=2 ||
        report.unstaged_allocated!=1 || report.unstaged_allocated_bytes!=32 ||
        report.relocation_candidate_sections!=1 || report.relocation_candidate_bytes!=36 ||
        !report.entry_staged || report.sda_staged || !report.sda2_staged) return 1;
    if (xboxrpx::r5c_is_staged(0x10000010u,prepared)) return 2; // end exclusive
    if (!xboxrpx::r5c_is_staged(0x1000000fu,prepared)) return 3;
    extra.name=".data"; // wrong address still must not be counted as staged
    if (xboxrpx::r5c_section_staged(extra,prepared)) return 4;
    std::puts("R5C synthetic section and SDA coverage audit PASSED");
    return 0;
}
