#include "RpxProbe.h"
#include "core/rpx_inspect.h"
#include "core/rpx_stage.h"
#if defined(NSMBU_R5_CPU_BOOTSTRAP)
#include "core/r5_startup_metadata.h"
#include "bootfirst_api.h"
#if defined(NSMBU_R5C_LINK_AUDIT)
#include "core/r5c_link_audit.h"
#if defined(NSMBU_R5D_RELOC_AUDIT)
#include "core/r5d_relocation_audit.h"
#endif
#endif
#endif
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

void run(xboxguest::GuestVm& guest, void (*log)(const char*),
         const char* requiredBuild) {
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
        // A linked native USA_V0 game must only stage its identical USA_V0 RPX.
        // Other recognized versions are not interchangeable with its C code.
        if (requiredBuild && (!known || std::strcmp(known, requiredBuild) != 0)) {
            log("[rpx/R5] LOCAL staging REFUSED: RPX SHA256 does not match the linked game's required build");
            return;
        }
        if (requiredBuild && image.entry != 0x02AA2E50u) {
            log("[rpx/R5] LOCAL staging REFUSED: RPX entrypoint does not match USA_V0 recompilation");
            return;
        }
        // Only byte-identical, known builds may be staged in this experiment.
        // Never execute source PowerPC from these pages; game function
        // recompilation and HLE imports are not integrated yet.
        if (!known) {
            log("[rpx/0.7] LOCAL staging SKIPPED: unrecognized RPX SHA256");
        } else if (!guest.active()) {
            log("[rpx/0.7] LOCAL staging SKIPPED: fixed guest VM unavailable");
        } else {
            log("[rpx/0.7] Phase 1: decode eligible sections to CPU buffers (no guest writes)");
            std::vector<xboxrpx::PreparedSection> sections;
            if (!xboxrpx::prepare_sections(data.data(), data.size(), image,
                                           sections, error
#if defined(NSMBU_R5E_BOOT_PARITY)
                                           , requiredBuild != nullptr
#endif
                                           )) {
                log("[rpx/0.7] LOCAL section preparation FAILED; no guest writes");
                log(error.c_str());
            } else {
                char detail[220] = {};
                size_t total = 0;
                for (const auto& sec : sections) total += sec.bytes.size();
                sprintf_s(detail,
                          "[rpx/0.7] staging VALIDATED: sections=%zu bytes=%zu, no overlaps",
                          sections.size(), total);
                log(detail);
                log("[rpx/0.7] Phase 2: commit sparse guest pages and copy checked bytes");
                size_t completed = 0;
                size_t written = 0;
                bool mappingOk = true;
#if defined(NSMBU_R5E_BOOT_PARITY)
                if (requiredBuild) {
                    uint32_t mem2Start = 0, mem2Bytes = 0;
                    if (!xboxrpx::boot_mem2_span(sections, mem2Start, mem2Bytes)) {
                        mappingOk = false;
                        log("[rpx/R5E] MEM2 span REFUSED: missing or exceeds 32 MiB safe budget");
                    } else if (!guest.commit(mem2Start, mem2Bytes)) {
                        mappingOk = false;
                        sprintf_s(detail,
                            "[rpx/R5E] MEM2 contiguous commit FAILED Win32=%lu",
                            static_cast<unsigned long>(guest.last_error()));
                        log(detail);
                    } else {
                        std::memset(guest.ptr(mem2Start), 0, mem2Bytes);
                        sprintf_s(detail,
                            "[rpx/R5E] MEM2 contiguous zero-fill PASS: first=%08X bytes=%u",
                            mem2Start, mem2Bytes);
                        log(detail);
                        log("[rpx/R5E] .syscall + .module_id opt-in stages alongside game data");
                    }
                }
#endif
                for (const auto& sec : sections) {
                    if (!mappingOk) break;
                    if (!guest.commit(sec.address, sec.bytes.size())) {
                        sprintf_s(detail,
                            "[rpx/0.7] guest commit FAILED: %s addr=%08X len=%zu Win32=%lu",
                            sec.name.c_str(), sec.address, sec.bytes.size(),
                            static_cast<unsigned long>(guest.last_error()));
                        log(detail);
                        mappingOk = false;
                        break;
                    }
                    uint8_t* dest = guest.ptr(sec.address);
                    std::memcpy(dest, sec.bytes.data(), sec.bytes.size());
                    const uint32_t guestCrc = xboxrpx::crc32(dest, sec.bytes.size());
                    const bool verified = guestCrc == sec.crc32 &&
                        std::memcmp(dest, sec.bytes.data(), sec.bytes.size()) == 0;
                    sprintf_s(detail,
                        "[rpx/0.7] %s: guest=%08X bytes=%zu crc32=%08X %s%s",
                        sec.name.c_str(), sec.address, sec.bytes.size(), guestCrc,
                        verified ? "VERIFIED" : "FAILED",
                        sec.zero_fill ? " (NOBITS zero-fill)" : "");
                    log(detail);
                    if (!verified) { mappingOk = false; break; }
                    ++completed;
                    written += sec.bytes.size();
                }
                sprintf_s(detail,
                    "[rpx/0.7] LOCAL section mapping %s: %zu/%zu sections, %zu bytes",
                    mappingOk ? "PASSED" : "INCOMPLETE",
                    completed, sections.size(), written);
                log(detail);
#if defined(NSMBU_R5_CPU_BOOTSTRAP)
                if (mappingOk && completed == sections.size() && requiredBuild) {
                    xboxrpx::StartupMetadata startup{};
                    if (!xboxrpx::read_startup_metadata(data.data(), data.size(),
                                                       image, startup, error)) {
                        log("[rpx/R5B] CPU bootstrap BLOCKED: invalid RPX startup metadata");
                        log(error.c_str());
                    } else {
                        constexpr uint32_t kStackBase = 0x60000000u;
                        const uint32_t stackSize =
                            (startup.stack_reserved + 0xFFFFu) & ~0xFFFFu;
                        if (!guest.commit(kStackBase, stackSize)) {
                            sprintf_s(detail,
                                "[rpx/R5B] CPU bootstrap BLOCKED: guest stack commit Win32=%lu",
                                static_cast<unsigned long>(guest.last_error()));
                            log(detail);
                        } else {
                            std::memset(guest.ptr(kStackBase), 0, stackSize);
                            NSMBUR5CpuSnapshot cpu{};
                            const int code = nsmbu_r5_prepare_cpu(
                                startup.entry, kStackBase, stackSize,
                                startup.sda_base, startup.sda2_base, &cpu);
                            sprintf_s(detail,
                                "[rpx/R5B] CPU bootstrap: status=%d entry=%08X core=%u "
                                "r1=%08X r2=%08X r13=%08X lr=%08X stack=%08X+%08X",
                                code, cpu.entry, cpu.core, cpu.r1, cpu.r2,
                                cpu.r13, cpu.lr, kStackBase, stackSize);
                            log(detail);
                            if (code == 0) {
                                sprintf_s(detail,
                                    "[rpx/R5B] CPU float state: fpscr=%08X gqr2=%08X gqr3=%08X gqr4=%08X gqr5=%08X",
                                    cpu.fpscr, cpu.gqr2, cpu.gqr3, cpu.gqr4, cpu.gqr5);
                                log(detail);
                                log("[rpx/R5B] CPU BOOTSTRAP PASS: stack and real Cpu register state prepared");
#if defined(NSMBU_R5C_LINK_AUDIT)
                                const auto a = xboxrpx::r5c_audit(
                                    image, sections, cpu.r13, cpu.r2);
                                sprintf_s(detail,
                                    "[rpx/R5C] section audit: allocated=%u staged=%u "
                                    "not-staged=%u not-staged-bytes=%llu reloc-candidates=%u reloc-bytes=%llu",
                                    a.allocated_sections, a.staged_sections,
                                    a.unstaged_allocated,
                                    static_cast<unsigned long long>(a.unstaged_allocated_bytes),
                                    a.relocation_candidate_sections,
                                    static_cast<unsigned long long>(a.relocation_candidate_bytes));
                                log(detail);
                                sprintf_s(detail,
                                    "[rpx/R5C] coverage: entry=%08X staged=%u "
                                    "r2=%08X staged=%u r13=%08X staged=%u",
                                    cpu.entry, unsigned(a.entry_staged),
                                    cpu.r2, unsigned(a.sda2_staged),
                                    cpu.r13, unsigned(a.sda_staged));
                                log(detail);
                                if (!a.sda_staged || !a.sda2_staged) {
#if defined(NSMBU_R5D_RELOC_AUDIT)
                                    log("[rpx/R5D] SDA base itself not staged; bases are relative offsets, classify their *accessed ranges* first");
#else
                                    log("[rpx/R5C] FIRST EXECUTION BLOCKED: SDA base outside staged sections; classify memory regions first");
#endif
                                }
                                for (const auto& missing : image.sections) {
                                    if (!missing.allocated() || !missing.memory_bytes ||
                                        xboxrpx::r5c_section_staged(missing, sections))
                                        continue;
                                    sprintf_s(detail,
                                        "[rpx/R5C] allocated not-staged: %.64s type=%08X flags=%08X addr=%08X bytes=%u",
                                        missing.name.c_str(), missing.type,
                                        missing.flags, missing.address, missing.memory_bytes);
                                    log(detail);
                                }
                                log("[rpx/R5C] Candidate relocation sections are metadata only: no relocations applied");
#endif
#if defined(NSMBU_R5D_RELOC_AUDIT)
                                // Separate diagnostics from guest writes. This audit reads
                                // owner-local RPX relocation metadata after prior SHA, staging,
                                // and CPU checks; it never patches a relocation or executes PPC.
                                xboxrpx::R5DRelocAudit reloc{};
                                std::string relocError;
                                if (!xboxrpx::r5d_audit_relocations(
                                        data.data(), data.size(), image, sections,
                                        cpu.r13, cpu.r2, reloc, relocError)) {
                                    log("[rpx/R5D] relocation metadata audit FAILED (no game execution)");
                                    log(relocError.c_str());
                                } else {
                                    sprintf_s(detail,
                                        "[rpx/R5D] RELA audit: sections=%u REL-without-addend=%u records=%llu "
                                        "import-refs=%llu entry-window=%llu invalid-symbols=%llu out-of-target=%llu",
                                        reloc.rela_sections, reloc.rel_without_addend_sections,
                                        static_cast<unsigned long long>(reloc.records),
                                        static_cast<unsigned long long>(reloc.imported_symbols),
                                        static_cast<unsigned long long>(reloc.entry_window_references),
                                        static_cast<unsigned long long>(reloc.invalid_symbols),
                                        static_cast<unsigned long long>(reloc.target_outside_section));
                                    log(detail);
                                    sprintf_s(detail,
                                        "[rpx/R5D] PPC reloc types: ADDR32=%llu LO=%llu HI=%llu HA=%llu "
                                        "REL24=%llu imported-text-immediates=%llu imported-branches=%llu",
                                        static_cast<unsigned long long>(reloc.type_counts[1]),
                                        static_cast<unsigned long long>(reloc.type_counts[4]),
                                        static_cast<unsigned long long>(reloc.type_counts[5]),
                                        static_cast<unsigned long long>(reloc.type_counts[6]),
                                        static_cast<unsigned long long>(reloc.type_counts[10]),
                                        static_cast<unsigned long long>(reloc.text_immediate_candidates),
                                        static_cast<unsigned long long>(reloc.imported_branch_candidates));
                                    log(detail);
                                    sprintf_s(detail,
                                        "[rpx/R5D] SDA signed-16bit windows: r13-staged=%llu/65536 "
                                        "r2-staged=%llu/65536 (coverage, not access analysis)",
                                        static_cast<unsigned long long>(reloc.r13_window_staged_bytes),
                                        static_cast<unsigned long long>(reloc.r2_window_staged_bytes));
                                    log(detail);
                                    for (const auto& slice : reloc.slices) {
                                        sprintf_s(detail,
                                            "[rpx/R5D] %.42s -> %.42s entries=%u imported=%u "
                                            "text-imm=%u imported-branch=%u entry-window=%u outside=%u bad-sym=%u",
                                            slice.name.c_str(), slice.target.c_str(),
                                            slice.entries, slice.imported_symbols,
                                            slice.text_immediate_candidates,
                                            slice.imported_branch_candidates,
                                            slice.entry_window_references,
                                            slice.target_outside_section,
                                            slice.invalid_symbols);
                                        log(detail);
                                    }
                                    log("[rpx/R5D] RELOCATIONS NOT APPLIED. Imported .text immediates "
                                        "were considered by recomp/recomp.py; do not reapply blindly.");
#if defined(NSMBU_R7_ENTRY_PROBE)
                                    // Owner-explicit R7: execute the real
                                    // compiled game entrypoint, only after
                                    // verified exact USA_V0 RPX and R5B/R5D.
                                    // We deliberately STOP on unsupported
                                    // Cafe OS, unknown PPC, traps or SEH fault.
                                    // No fake successful service returns.
                                    if (requiredBuild &&
                                        reloc.invalid_symbols == 0 &&
                                        reloc.target_outside_section == 0 &&
                                        reloc.rela_sections == 3) {
                                        // Match upstream threads.cpp::create_thread:
                                        // real 0x6A0-byte OSThread in separate guest heap memory,
                                        // stack top/bottom, core1 affinity and a real thread ID.
                                        // No invented null pointer from OSGetCurrentThread.
                                        constexpr uint32_t kThreadAddress = 0x60100000u;
                                        constexpr uint32_t kMainStackBase = 0x60000000u;
                                        constexpr uint32_t kThreadCommitBytes = 0x10000u;
                                        if (!guest.commit(kThreadAddress, kThreadCommitBytes)) {
                                            sprintf_s(detail,
                                                "[rpx/R7B] SKIP: guest thread page commit Win32=%lu",
                                                static_cast<unsigned long>(guest.last_error()));
                                            log(detail);
                                        } else {
                                            std::memset(guest.ptr(kThreadAddress), 0, kThreadCommitBytes);
                                            const int threadRc = nsmbu_r7_prepare_main_thread(
                                                kThreadAddress, kMainStackBase,
                                                kThreadAddress, startup.entry);
                                            if (threadRc != 0) {
                                                sprintf_s(detail,
                                                    "[rpx/R7B] SKIP: invalid guest OSThread status=%d",
                                                    threadRc);
                                                log(detail);
                                            } else {
                                                sprintf_s(detail,
                                                    "[rpx/R7B] MAIN OSThread READY: addr=%08X bytes=%u "
                                                    "stack=%08X..%08X core=1, no scheduler yet",
                                                    kThreadAddress, 0x6A0u,
                                                    kMainStackBase, kThreadAddress);
                                                log(detail);
                                                log("[rpx/R7] BEGIN real entry with three bounded coreinit services");
                                                NSMBUR7EntryReport boot{};
                                                const int bootRc = nsmbu_r7_try_entry(&boot);
                                                sprintf_s(detail,
                                                    "[rpx/R7] STOP: setup=%d status=%u entry=%08X "
                                                    "last-pc=%08X lr=%08X r3=%08X entered=%u loops=%u coreinit-handled=%u",
                                                    bootRc, boot.status, boot.entry, boot.last_pc,
                                                    boot.last_lr, boot.final_r3,
                                                    boot.functions_entered, boot.loop_ticks,
                                                    boot.handled_coreinit_calls);
                                                log(detail);
                                                sprintf_s(detail,
                                                    "[rpx/R7] FIRST BLOCKER: exception=%08X host-pc=%llX reason=%.120s",
                                                    boot.seh_code,
                                                    static_cast<unsigned long long>(boot.exception_address),
                                                    boot.stop_reason);
                                                log(detail);
                                                log("[rpx/R7] ENTRY attempt ended; menu/gameplay/frame NOT confirmed");
                                            }
                                        }
                                    } else {
                                        log("[rpx/R7] SKIP: exact build or relocation integrity gate failed");
                                    }
#endif
#if defined(NSMBU_R6A_EXECUTE_LEAF)
                                    // The user has explicitly opted into exactly one
                                    // pure native game leaf. Both section contents
                                    // and recompilation match the known USA_V0 SHA.
                                    // No entrypoint, imports or Cafe OS may run.
                                    if (reloc.invalid_symbols == 0 &&
                                        reloc.target_outside_section == 0 &&
                                        reloc.rela_sections == 3) {
                                        const xboxrpx::PreparedSection* gameText = nullptr;
                                        for (const auto& loaded : sections) {
                                            if (loaded.name == ".text") {
                                                gameText = &loaded;
                                                break;
                                            }
                                        }
                                        if (gameText && gameText->bytes.size() <= UINT32_MAX) {
                                            NSMBUR6LeafReport leaf{};
                                            const int leafResult = nsmbu_r6_run_pure_leaf(
                                                gameText->bytes.data(), gameText->address,
                                                static_cast<uint32_t>(gameText->bytes.size()), &leaf);
                                            if (leafResult == 0) {
                                                sprintf_s(detail,
                                                    "[rpx/R6A] REAL GAME LEAF EXECUTED: addr=%08X pattern=%u "
                                                    "r3=%08X expected=%08X stack-preserved=%u",
                                                    leaf.candidate, leaf.pattern, leaf.actual_r3,
                                                    leaf.expected_r3, leaf.stack_preserved);
                                                log(detail);
                                            } else if (leafResult == 1) {
                                                log("[rpx/R6A] NO vetted leaf in USA_V0 table; game execution not attempted");
                                            } else {
                                                sprintf_s(detail,
                                                    "[rpx/R6A] LEAF EXECUTION FAILED: status=%d candidate=%08X "
                                                    "r3=%08X expected=%08X stack=%u",
                                                    leafResult, leaf.candidate, leaf.actual_r3,
                                                    leaf.expected_r3, leaf.stack_preserved);
                                                log(detail);
                                            }
                                        } else {
                                            log("[rpx/R6A] SKIP: no small staged .text section");
                                        }
                                    } else {
                                        log("[rpx/R6A] SKIP: relocation metadata did not pass integrity gate");
                                    }
#endif
                                }
#endif
#if defined(NSMBU_R7_ENTRY_PROBE)
                                log("[rpx/R7] Game ENTRY was attempted with missing Cafe OS; normal gameplay remains unavailable");
#elif defined(NSMBU_R6A_EXECUTE_LEAF)
                                log("[rpx/R6A] Game ENTRY still disabled: Cafe OS/imports/scheduler not initialized");
#else
                                log("[rpx/R5B] EXECUTION STILL DISABLED: imports, relocations and Cafe OS not initialized");
#endif
                            } else {
                                log("[rpx/R5B] CPU BOOTSTRAP FAILED: unsafe startup configuration");
                            }
                        }
                    }
                }
#endif
            }
        }
#if defined(NSMBU_R7_ENTRY_PROBE)
        log("[rpx/R7] Real game entry was attempted; complete HLE/scheduler/GX2 still unavailable");
#elif defined(NSMBU_R6A_EXECUTE_LEAF)
        log("[rpx/R6A] Game ENTRY/HLE disabled; only the vetted optional leaf may have executed");
#else
        log("[rpx/0.7] LOCAL game execution DISABLED: no entry-point jump, no relocations/HLE");
#endif
    } catch (...) {
        log("[rpx/0.6] LOCAL UWP file inspection raised an exception");
    }
}
} // namespace xboxrpxprobe
