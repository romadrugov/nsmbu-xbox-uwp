#include "rpx_stage.h"
#include <algorithm>
#include <utility>

namespace xboxrpx {
namespace {
constexpr uint64_t kGuestLimit = 0x100000000ull;
constexpr uint64_t kMaxStaged = 48ull * 1024ull * 1024ull;

bool chosen(const std::string& name) {
    return name == ".text" || name == ".rodata" || name == ".rodata1" ||
           name == ".data" || name == ".data1" ||
           name == ".bss" || name == ".sdata" || name == ".sdata2" ||
           name == ".sbss" || name == ".sbss2";
}
bool fail(std::string& error, const char* why) {
    error = why;
    return false;
}
} // namespace

uint32_t crc32(const uint8_t* bytes, size_t count) {
    uint32_t result = 0xFFFFFFFFu;
    if (!bytes && count) return 0u;
    for (size_t i=0; i<count; ++i) {
        result ^= bytes[i];
        for (unsigned bit=0; bit<8; ++bit)
            result = (result >> 1u) ^ ((result & 1u) ? 0xEDB88320u : 0u);
    }
    return ~result;
}

bool prepare_sections(const uint8_t* file, size_t file_size, const Image& image,
                      std::vector<PreparedSection>& result, std::string& error) {
    if (!file || image.sections.empty())
        return fail(error, "RPX staging requires a previously validated image");
    std::vector<PreparedSection> staged;
    uint64_t totalBytes = 0;
    bool hasText = false;
    for (const Section& section : image.sections) {
        if (!chosen(section.name)) continue;
        if (!section.allocated() || section.memory_bytes == 0)
            return fail(error, "requested RPX section is not allocated or has zero size");
        if (section.type != 1u && section.type != 8u)
            return fail(error, "requested RPX section has unsupported section type");
        if (section.name == ".text") {
            if (hasText || !section.executable())
                return fail(error, "RPX executable .text section is missing or duplicated");
            hasText = true;
        }
        const uint64_t start = section.address;
        const uint64_t end = start + section.memory_bytes;
        if (start < 0x10000ull || end > kGuestLimit || end <= start)
            return fail(error, "RPX section guest address outside 4GiB range");
        if (section.memory_bytes > kMaxStaged - totalBytes)
            return fail(error, "selected RPX section budget exceeds 48 MiB");
        for (const auto& previous : staged) {
            const uint64_t prevStart = previous.address;
            const uint64_t prevEnd = prevStart + previous.bytes.size();
            if (start < prevEnd && prevStart < end)
                return fail(error, "selected RPX sections overlap in guest memory");
        }
        PreparedSection next;
        next.name = section.name;
        next.address = section.address;
        next.zero_fill = section.type == 8u;
        if (!decode_section(file, file_size, section, next.bytes, error)) {
            error = "could not decode " + section.name + ": " + error;
            return false;
        }
        if (next.bytes.size() != section.memory_bytes)
            return fail(error, "decoded RPX section size does not match header");
        next.crc32 = crc32(next.bytes.data(), next.bytes.size());
        totalBytes += next.bytes.size();
        staged.push_back(std::move(next));
    }
    if (!hasText)
        return fail(error, "RPX staging requires allocated executable .text");
    result.swap(staged); // All checks passed; caller output unchanged otherwise.
    error.clear();
    return true;
}
} // namespace xboxrpx
