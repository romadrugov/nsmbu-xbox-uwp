#include "rpx_inspect.h"
#include <cstring>
#include <utility>

namespace xboxrpx {
namespace {
constexpr size_t kElfHeader = 52;
constexpr size_t kSectionHeader = 40;
constexpr size_t kMaxFileBytes = 128u * 1024u * 1024u;
constexpr uint64_t kMaxMappedBytes = 512ull * 1024ull * 1024ull;
constexpr uint32_t kZlibFlag = 0x08000000u;
constexpr uint32_t kNoBits = 8u;

uint16_t u16(const uint8_t* b) {
    return uint16_t((uint16_t(b[0]) << 8) | b[1]);
}
uint32_t u32(const uint8_t* b) {
    return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) |
           (uint32_t(b[2]) << 8) | b[3];
}
bool range(size_t total, uint64_t offset, uint64_t len) {
    return offset <= total && len <= total - offset;
}
bool fail(std::string& reason, const char* msg) {
    reason = msg;
    return false;
}
struct Raw {
    uint32_t name, type, flags, addr, off, file_size, align;
};
} // namespace

const Section* section_named(const Image& image, const char* name) {
    for (const auto& section : image.sections)
        if (section.name == name) return &section;
    return nullptr;
}

bool inspect(const uint8_t* bytes, size_t count,
             Image& output, std::string& error) {
    if (!bytes || count < kElfHeader || count > kMaxFileBytes)
        return fail(error, "missing file, truncated ELF header or image >128 MiB");
    if (std::memcmp(bytes, "\x7f" "ELF", 4) != 0 ||
        bytes[4] != 1 || bytes[5] != 2 || bytes[6] != 1)
        return fail(error, "not ELF32 big-endian v1");
    if (u16(bytes + 16) != 2 || u16(bytes + 18) != 20 ||
        u32(bytes + 20) != 1)
        return fail(error, "ELF is not executable PowerPC v1");
    const uint32_t entry = u32(bytes + 24);
    const uint32_t shoff = u32(bytes + 32);
    const uint16_t shentsize = u16(bytes + 46);
    const uint16_t shnum = u16(bytes + 48);
    const uint16_t shstrndx = u16(bytes + 50);
    if (entry == 0 || shnum < 2 || shnum > 512 ||
        shentsize < kSectionHeader || shentsize > 256 ||
        shstrndx >= shnum ||
        !range(count, shoff, uint64_t(shentsize) * shnum))
        return fail(error, "bad section table or entry point");

    std::vector<Raw> raw(shnum);
    for (unsigned i=0; i<shnum; ++i) {
        const uint8_t* p = bytes + shoff + size_t(i)*shentsize;
        Raw r{u32(p),u32(p+4),u32(p+8),u32(p+12),u32(p+16),
              u32(p+20),u32(p+32)};
        if (r.type != kNoBits && !range(count, r.off, r.file_size))
            return fail(error, "section payload outside file");
        if (r.align && (r.align & (r.align - 1u)))
            return fail(error, "non power-of-two section alignment");
        raw[i] = r;
    }

    const Raw& str = raw[shstrndx];
    if (str.type == kNoBits || (str.flags & kZlibFlag) ||
        str.file_size == 0 || str.file_size > (1u<<20))
        return fail(error, "invalid section-name string table");
    const uint8_t* names = bytes + str.off;
    Image tmp;
    tmp.entry = entry;
    bool entryIsText = false;
    for (const Raw& r : raw) {
        if (r.name >= str.file_size)
            return fail(error, "section name offset outside string table");
        const char* name = reinterpret_cast<const char*>(names + r.name);
        const void* terminator = std::memchr(name, 0, str.file_size - r.name);
        if (!terminator)
            return fail(error, "unterminated section name");
        Section sec;
        sec.name.assign(name, static_cast<const char*>(terminator) - name);
        sec.type = r.type;
        sec.flags = r.flags;
        sec.address = r.addr;
        sec.file_offset = r.off;
        sec.file_bytes = r.file_size;
        sec.compressed = (r.flags & kZlibFlag) != 0u && r.type != kNoBits;
        sec.memory_bytes = r.file_size;
        if (sec.compressed) {
            if (r.file_size < 5)
                return fail(error, "compressed section too short");
            sec.memory_bytes = u32(bytes + r.off);
            if (sec.memory_bytes == 0)
                return fail(error, "invalid compressed section decoded size");
            ++tmp.compressed_count;
        }
        if (sec.allocated() && sec.memory_bytes) {
            const uint64_t end = uint64_t(sec.address) + sec.memory_bytes;
            if (sec.address < 0x10000u || end > 0x100000000ull)
                return fail(error, "mapped section address outside guest range");
            tmp.mapped_bytes += sec.memory_bytes;
            if (tmp.mapped_bytes > kMaxMappedBytes)
                return fail(error, "total mapped sections exceed 512 MiB");
            ++tmp.mapped_count;
            if (sec.executable() && entry >= sec.address && uint64_t(entry) < end)
                entryIsText = true;
        }
        tmp.sections.push_back(std::move(sec));
    }
    if (!entryIsText)
        return fail(error, "entry point not inside executable allocated section");
    output = std::move(tmp);
    error.clear();
    return true;
}
} // namespace xboxrpx
