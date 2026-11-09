#include "rpx_inspect.h"
#include <cstring>
#include <cstdio>
#include <utility>

// Portable MIT-licensed DEFLATE/zlib decoder. Only used for the bounded
// section-name table, never to load or execute guest code.
#define SINFL_NO_SIMD
#define SINFL_IMPLEMENTATION
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
// Upstream C89 single-header initializes POD structures with {0}.
// Keep our own code under -Werror while not rewriting vendored code.
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "third_party/sinfl.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

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
    const uint16_t elfType = u16(bytes + 16);
    const uint16_t machine = u16(bytes + 18);
    const uint32_t elfVersion = u32(bytes + 20);
    // Standard synthetic ELF files use ET_EXEC=2. Retail Wii U RPX/RPL
    // files are Cafe OS modified ELF32 objects and use 0xFE01 instead.
    // Do not accept arbitrary processor-specific or shared-object types.
    if (elfType != 2u && elfType != 0xFE01u) {
        char message[100] = {};
        std::snprintf(message, sizeof(message),
                      "unsupported ELF e_type=0x%04X (expected 0x0002 or Cafe OS 0xFE01)",
                      unsigned(elfType));
        return fail(error, message);
    }
    if (machine != 20u || elfVersion != 1u) {
        char message[120] = {};
        std::snprintf(message, sizeof(message),
                      "unsupported ELF e_machine=0x%04X e_version=0x%08X (expected PPC=0x0014, v1)",
                      unsigned(machine), unsigned(elfVersion));
        return fail(error, message);
    }
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
    if (str.type == kNoBits || str.file_size == 0 ||
        str.file_size > (1u<<20))
        return fail(error, "invalid section-name string table");

    std::vector<uint8_t> decodedNames;
    if ((str.flags & kZlibFlag) != 0u) {
        // Cafe OS SHF_RPL_ZLIB: big-endian uncompressed size (4 bytes),
        // followed by a normal RFC1950 zlib stream (DEFLATE + Adler32).
        if (str.file_size < 10)
            return fail(error, "compressed section-name table too short");
        const uint32_t outputLen = u32(bytes + str.off);
        if (outputLen == 0 || outputLen > (1u<<20))
            return fail(error, "invalid decompressed section-name table size");
        const size_t streamLen = str.file_size - 4u;
        const uint8_t* stream = bytes + str.off + 4u;
        const unsigned zlibHeader = (unsigned(stream[0]) << 8u) | stream[1];
        if ((stream[0] & 15u) != 8u || (stream[0] >> 4u) > 7u ||
            (zlibHeader % 31u) != 0u || (stream[1] & 0x20u) != 0u)
            return fail(error, "unsupported zlib header in section-name table");
        // sinfl may use speculative byte reads/copies at bitstream edges:
        // pad both buffers; cap actual decompressed data to 1 MiB.
        std::vector<uint8_t> paddedInput(streamLen + 64u, 0);
        std::memcpy(paddedInput.data(), stream, streamLen);
        std::vector<uint8_t> paddedOutput(size_t(outputLen) + 64u, 0);
        const int got = zsinflate(paddedOutput.data(), int(outputLen),
                                  paddedInput.data(), int(streamLen));
        if (got != int(outputLen))
            return fail(error, "section-name zlib decompression failed (size/checksum)");
        decodedNames.assign(paddedOutput.begin(), paddedOutput.begin() + outputLen);
    } else {
        decodedNames.assign(bytes + str.off, bytes + str.off + str.file_size);
    }
    if (decodedNames.empty() || decodedNames[0] != 0u)
        return fail(error, "section-name table missing leading NUL");
    const uint8_t* names = decodedNames.data();
    const size_t namesCount = decodedNames.size();
    Image tmp;
    tmp.section_names_compressed = (str.flags & kZlibFlag) != 0u;
    tmp.section_names_decoded_bytes = namesCount;
    tmp.elf_type = elfType;
    tmp.machine = machine;
    tmp.os_abi = bytes[7];
    tmp.entry = entry;
    bool entryIsText = false;
    for (const Raw& r : raw) {
        if (r.name >= namesCount)
            return fail(error, "section name offset outside string table");
        const char* name = reinterpret_cast<const char*>(names + r.name);
        const void* terminator = std::memchr(name, 0, namesCount - r.name);
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
