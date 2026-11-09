#include "RpxProbe.h"
#include "core/rpx_inspect.h"
#include "generated/PublicRpxFixture.h"
#include <windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace xboxrpxprobe {
namespace {
constexpr size_t kMaxLocal = 96u * 1024u * 1024u;

void log_image(const xboxrpx::Image& image, void (*log)(const char*), const char* which) {
    char line[270] = {};
    sprintf_s(line, "[rpx/0.6] %s: ELF type=%04X machine=%04X osabi=%02X entry=%08X sections=%zu mapped=%zu mapped-bytes=%llu compressed=%zu",
        which, unsigned(image.elf_type), unsigned(image.machine), unsigned(image.os_abi),
        image.entry, image.sections.size(), image.mapped_count,
        static_cast<unsigned long long>(image.mapped_bytes), image.compressed_count);
    log(line);
    sprintf_s(line, "[rpx/0.6] %s section-name table: %s, decoded-bytes=%zu",
              which, image.section_names_compressed ? "zlib-compressed" : "uncompressed",
              image.section_names_decoded_bytes);
    log(line);
    if (const auto* text = xboxrpx::section_named(image, ".text")) {
        sprintf_s(line, "[rpx/0.6] %s .text: address=%08X file-bytes=%u decoded-bytes=%u compressed=%s",
            which, text->address, text->file_bytes, text->memory_bytes,
            text->compressed ? "yes" : "no");
        log(line);
    }
}

void log_elf_header_preview(const std::vector<uint8_t>& bytes, void (*log)(const char*)) {
    // Display the raw ELF identification even if later section validation
    // rejects the file. No Nintendo data or section contents are logged.
    if (bytes.size() < 52 || std::memcmp(bytes.data(), "\x7f" "ELF", 4) != 0) {
        log("[rpx/0.6] LOCAL header: missing or truncated ELF magic");
        return;
    }
    auto be16 = [&](size_t n) {
        return unsigned(bytes[n]) * 256u + unsigned(bytes[n+1]);
    };
    auto be32 = [&](size_t n) {
        return (unsigned(bytes[n]) << 24u) | (unsigned(bytes[n+1]) << 16u) |
               (unsigned(bytes[n+2]) << 8u) | unsigned(bytes[n+3]);
    };
    char line[280] = {};
    sprintf_s(line,
              "[rpx/0.6] LOCAL ELF header: class=%u endian=%u osabi=%02X type=%04X machine=%04X version=%u entry=%08X shoff=%08X shnum=%u shstrndx=%u",
              unsigned(bytes[4]), unsigned(bytes[5]), unsigned(bytes[7]),
              be16(16), be16(18), be32(20), be32(24), be32(32),
              be16(48), be16(50));
    log(line);
}

bool read_local(const std::wstring& path, std::vector<uint8_t>& bytes, DWORD& error) {
    error = 0;
    HANDLE file = CreateFile2(path.c_str(), FILE_READ_DATA, FILE_SHARE_READ,
                              OPEN_EXISTING, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return false;
    }
    LARGE_INTEGER length = {};
    if (!GetFileSizeEx(file, &length) || length.QuadPart <= 0 ||
        static_cast<unsigned long long>(length.QuadPart) > kMaxLocal) {
        error = ERROR_FILE_TOO_LARGE;
        CloseHandle(file);
        return false;
    }
    std::vector<uint8_t> content(static_cast<size_t>(length.QuadPart));
    size_t offset = 0;
    while (offset < content.size()) {
        const DWORD take = static_cast<DWORD>(
            (content.size()-offset) > 1024u*1024u ? 1024u*1024u : content.size()-offset);
        DWORD read = 0;
        if (!ReadFile(file, content.data() + offset, take, &read, nullptr) || read == 0) {
            error = GetLastError();
            if (!error) error = ERROR_HANDLE_EOF;
            CloseHandle(file);
            return false;
        }
        offset += read;
    }
    CloseHandle(file);
    bytes.swap(content);
    return true;
}

bool sha256_hex(const std::vector<uint8_t>& input, std::string& hex) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32] = {};
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    if (ok) ok = BCryptHashData(hash,
                    const_cast<PUCHAR>(input.data()), static_cast<ULONG>(input.size()), 0) >= 0;
    if (ok) ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) return false;
    static const char chars[]="0123456789abcdef";
    hex.clear();
    for (unsigned char b : digest) {
        hex += chars[b >> 4];
        hex += chars[b & 15];
    }
    return true;
}
} // namespace

void run(xboxguest::GuestVm& guest, void (*log)(const char*)) {
    if (!log) return;
    log("[rpx/0.6] Phase 1: validate public synthetic PowerPC ELF/RPX");
    xboxrpx::Image synthetic;
    std::string error;
    if (!xboxrpx::inspect(xboxrpxfixture::kSyntheticRpx,
                          sizeof(xboxrpxfixture::kSyntheticRpx), synthetic, error)) {
        log("[rpx/0.6] PUBLIC synthetic RPX structural validation FAILED");
        log(error.c_str());
        return;
    }
    log_image(synthetic, log, "PUBLIC");
    const auto* section = xboxrpx::section_named(synthetic, ".text");
    if (guest.active() && section && !section->compressed &&
        section->memory_bytes == section->file_bytes &&
        guest.commit(section->address, section->memory_bytes)) {
        uint8_t* dest = guest.ptr(section->address);
        const uint8_t* src = xboxrpxfixture::kSyntheticRpx + section->file_offset;
        std::memcpy(dest, src, section->file_bytes);
        log(std::memcmp(dest, src, section->file_bytes) == 0
            ? "[rpx/0.6] PUBLIC .text mapped into sparse guest memory PASSED"
            : "[rpx/0.6] PUBLIC .text guest memory mismatch FAILED");
    } else {
        log("[rpx/0.6] PUBLIC guest mapping SKIPPED (VM unavailable or invalid fixture section)");
    }

    log("[rpx/0.6] Phase 2: inspect optional LOCAL red-pro2.rpx (metadata only)");
    try {
        auto folder = Windows::Storage::ApplicationData::Current->LocalFolder;
        std::wstring path(folder->Path->Data());
        path += L"\\red-pro2.rpx";
        std::vector<uint8_t> data;
        DWORD readError = 0;
        if (!read_local(path, data, readError)) {
            if (readError == ERROR_FILE_NOT_FOUND || readError == ERROR_PATH_NOT_FOUND) {
                log("[rpx/0.6] LOCAL red-pro2.rpx not provided; PUBLIC fixture test only");
            } else {
                char line[160] = {};
                sprintf_s(line, "[rpx/0.6] LOCAL file read FAILED: Win32=%lu",
                          static_cast<unsigned long>(readError));
                log(line);
            }
            return;
        }
        char line[180] = {};
        sprintf_s(line, "[rpx/0.6] LOCAL image read: %zu bytes (not executed)", data.size());
        log(line);
        log_elf_header_preview(data, log);
        // Hash-based build identification is independent of ELF section
        // validation, so even a rejected RPX still reports its known version.
        std::string fingerprint;
        if (!sha256_hex(data, fingerprint)) {
            log("[rpx/0.6] LOCAL SHA256 calculation FAILED");
            return;
        }
        const char* known = nullptr;
        for (const auto& b : xboxrpxfixture::kKnownBuilds) {
            if (fingerprint == b.sha256) {
                known = b.name;
                break;
            }
        }
        if (known) {
            sprintf_s(line, "[rpx/0.6] LOCAL SHA256 recognized: %s (metadata only)", known);
            log(line);
        } else {
            log("[rpx/0.6] LOCAL SHA256 NOT recognized by recomp/builds.py; no code execution");
        }

        xboxrpx::Image image;
        if (!xboxrpx::inspect(data.data(), data.size(), image, error)) {
            log("[rpx/0.6] LOCAL structural validation FAILED");
            log(error.c_str());
            return;
        }
        log_image(image, log, "LOCAL");
        // Explicitly no decompression, relocations, guest mapping or execution
        // of the locally uploaded copyrighted image in this version.
        log("[rpx/0.6] LOCAL inspection finished; execution DISABLED");
    } catch (...) {
        log("[rpx/0.6] LOCAL UWP file inspection raised an exception");
    }
}
} // namespace xboxrpxprobe
