#include "r5_startup_metadata.h"
#include <algorithm>
#include <vector>

namespace xboxrpx {
namespace {
constexpr uint32_t kCafeModuleInfo = 0x80000004u;
constexpr uint32_t kMinStack = 0x00100000u;    // matches threads::run_main
constexpr uint32_t kMaxStack = 0x01000000u;    // capped for Xbox diagnostic
uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
bool fail(std::string& reason, const char* message) {
    reason = message;
    return false;
}
bool is_mem2_pointer(uint32_t addr) {
    return addr >= 0x10000000u && addr < 0x50000000u && (addr & 3u) == 0;
}
}

bool read_startup_metadata(const uint8_t* file, size_t length,
                           const Image& inspected,
                           StartupMetadata& output,
                           std::string& error) {
    if (!file || !length || inspected.elf_type != 0xFE01u ||
        inspected.machine != 20u || !inspected.entry)
        return fail(error, "R5B requires an inspected Cafe OS PPC RPX");
    const Section* info = nullptr;
    for (const auto& section : inspected.sections) {
        if (section.type != kCafeModuleInfo) continue;
        if (info) return fail(error, "multiple Cafe OS module info sections");
        info = &section;
    }
    if (!info) return fail(error, "Cafe OS module info section missing");
    std::vector<uint8_t> bytes;
    if (!decode_section(file, length, *info, bytes, error))
        return false;
    if (bytes.size() < 0x30u)
        return fail(error, "Cafe OS module info truncated before SDA and stack fields");
    StartupMetadata result{};
    result.entry = inspected.entry;
    result.sda_base = be32(bytes.data() + 0x24u);
    result.sda2_base = be32(bytes.data() + 0x28u);
    result.stack_requested = be32(bytes.data() + 0x2Cu);
    if (!is_mem2_pointer(result.sda_base) ||
        !is_mem2_pointer(result.sda2_base))
        return fail(error, "Cafe OS module SDA bases must be aligned MEM2 addresses");
    if (result.stack_requested > kMaxStack)
        return fail(error, "Cafe OS requested stack exceeds Xbox R5B safety budget");
    result.stack_reserved = std::max(result.stack_requested, kMinStack);
    output = result;
    error.clear();
    return true;
}
