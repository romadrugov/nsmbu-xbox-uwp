// PUBLIC handcrafted .dinfo fixture. No Nintendo RPX data.
#include "core/r5_startup_metadata.h"
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

static void be32(std::vector<uint8_t>& d, size_t at, uint32_t n) {
    d[at] = uint8_t(n >> 24);
    d[at + 1] = uint8_t(n >> 16);
    d[at + 2] = uint8_t(n >> 8);
    d[at + 3] = uint8_t(n);
}
int main() {
    std::vector<uint8_t> d(0x30u, 0u);
    be32(d, 0x24u, 0x101A6240u); // crafted, not a real RPX
    be32(d, 0x28u, 0x10004000u);
    be32(d, 0x2Cu, 0x00040000u);
    xboxrpx::Image image{};
    image.elf_type = 0xFE01u;
    image.machine = 20u;
    image.entry = 0x02AA2E50u;
    xboxrpx::Section m{};
    m.name = ".dinfo";
    m.type = 0x80000004u;
    m.file_offset = 0u;
    m.file_bytes = uint32_t(d.size());
    m.memory_bytes = uint32_t(d.size());
    image.sections.push_back(m);
    xboxrpx::StartupMetadata out{};
    std::string err;
    auto parse = [&]() {
        return xboxrpx::read_startup_metadata(d.data(),d.size(),image,out,err);
    };
    if (!parse() || out.entry != image.entry ||
        out.sda_base != 0x101A6240u || out.sda2_base != 0x10004000u ||
        out.stack_requested != 0x00040000u ||
        out.stack_reserved != 0x00100000u) return 1;
    const auto prior = out;
    be32(d, 0x2Cu, 0x02000000u);
    if (parse() || out.stack_reserved != prior.stack_reserved || err.empty())
        return 2; // >16 MiB requests blocked, output unchanged
    be32(d, 0x2Cu, 0x00040000u);
    be32(d, 0x24u, 0u);
    if (parse() || out.sda_base != prior.sda_base) return 3;
    be32(d, 0x24u, 0x101A6240u);
    image.sections.push_back(m);
    if (parse() || err.find("multiple") == std::string::npos) return 4;
    image.sections.pop_back();
    image.sections[0].file_bytes = 0x2Fu;
    image.sections[0].memory_bytes = 0x2Fu;
    if (parse()) return 5;
    image.sections[0].file_bytes = 0x30u;
    image.sections[0].memory_bytes = 0x30u;
    image.elf_type = 2u;
    if (parse()) return 6;
    image.elf_type = 0xFE01u;
    if (!parse() || !err.empty()) return 7;
    std::puts("R5B synthetic CPU startup metadata checks PASSED");
    return 0;
}
