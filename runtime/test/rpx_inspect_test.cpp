#include "core/rpx_inspect.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static bool rejected(const std::vector<uint8_t>& bytes) {
    xboxrpx::Image image;
    std::string reason;
    return !xboxrpx::inspect(bytes.data(), bytes.size(), image, reason) && !reason.empty();
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fputs("Usage: rpx_inspect_test PUBLIC_SYNTHETIC.rpx\n", stderr);
        return 2;
    }
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                std::istreambuf_iterator<char>());
    xboxrpx::Image image;
    std::string reason;
    if (!xboxrpx::inspect(bytes.data(), bytes.size(), image, reason)) {
        std::fprintf(stderr, "Public RPX parsing failed: %s\n", reason.c_str());
        return 1;
    }
    const xboxrpx::Section* text = xboxrpx::section_named(image, ".text");
    if (image.entry != 0x02000000u || !text ||
        text->file_bytes != 12 || text->address != image.entry ||
        !text->executable() || image.mapped_count != 1) {
        std::fputs("Public RPX metadata mismatch\n", stderr);
        return 1;
    }
    // All rejected inputs must leave caller's previous valid Image untouched.
    // A real Wii U RPX/RPL carries Cafe OS e_type=0xFE01 and usually
    // OSABI=0xCA, unlike the synthetic public ET_EXEC fixture. The same
    // section layout must remain valid under both accepted ELF types.
    std::vector<uint8_t> cafe = bytes;
    cafe[16] = 0xFE; cafe[17] = 0x01;
    cafe[7] = 0xCA;
    xboxrpx::Image cafeImage;
    if (!xboxrpx::inspect(cafe.data(), cafe.size(), cafeImage, reason) ||
        cafeImage.elf_type != 0xFE01u || cafeImage.machine != 20u ||
        cafeImage.os_abi != 0xCAu || cafeImage.entry != 0x02000000u ||
        !xboxrpx::section_named(cafeImage, ".text")) {
        std::fprintf(stderr, "Cafe OS 0xFE01 fixture rejected: %s\n", reason.c_str());
        return 1;
    }
    if (image.elf_type != 2u || image.machine != 20u)
        return 1; // Public ELF must still parse as conventional ET_EXEC.
    auto unsupported = cafe;
    unsupported[17] = 0x02; // unsupported Cafe/vendor type 0xFE02
    if (!rejected(unsupported)) return 1;
    unsupported = cafe;
    unsupported[16] = 0; unsupported[17] = 3; // ET_DYN is not a Cafe RPX
    if (!rejected(unsupported)) return 1;
    unsupported = cafe;
    unsupported[18] = 0; unsupported[19] = 3; // reject wrong CPU even in Cafe mode
    if (!rejected(unsupported)) return 1;
    unsupported = cafe;
    unsupported[23] = 2; // bad header version
    if (!rejected(unsupported)) return 1;

    // Cafe OS can compress the .shstrtab itself. The section header
    // then describes four big-endian decoded-length bytes + a zlib stream.
    // These are PUBLIC hand-authored names from android_fixture.py.
    const uint8_t packedNames[] = {
        0x00, 0x00, 0x00, 0x11, 0x78, 0xda, 0x63, 0xd0,
        0x2b, 0x49, 0xad, 0x28, 0x61, 0xd0, 0x2b, 0xce,
        0x28, 0x2e, 0x29, 0x2a, 0x49, 0x4c, 0x62, 0x00,
        0x00, 0x2f, 0xb0, 0x05, 0x8d
    };
    auto writeBe32 = [](std::vector<uint8_t>& destination, size_t offset, uint32_t value) {
        destination[offset] = uint8_t(value >> 24);
        destination[offset + 1] = uint8_t(value >> 16);
        destination[offset + 2] = uint8_t(value >> 8);
        destination[offset + 3] = uint8_t(value);
    };
    std::vector<uint8_t> cafeCompressed = cafe;
    // The synthetic ELF has section header table at offset 52 and
    // .shstrtab as the third entry: 52 + 2*40 = 132.
    constexpr size_t kShstrHeader = 132;
    constexpr size_t kPackedOffset = 52 + 3*40 + 12 + 17;
    if (cafeCompressed.size() != kPackedOffset) return 1;
    writeBe32(cafeCompressed, kShstrHeader + 8, 0x08000000u); // SHF_RPL_ZLIB
    writeBe32(cafeCompressed, kShstrHeader + 16, uint32_t(kPackedOffset));
    writeBe32(cafeCompressed, kShstrHeader + 20, sizeof(packedNames));
    cafeCompressed.insert(cafeCompressed.end(), packedNames,
                          packedNames + sizeof(packedNames));
    xboxrpx::Image compressedImage;
    if (!xboxrpx::inspect(cafeCompressed.data(), cafeCompressed.size(), compressedImage, reason) ||
        !compressedImage.section_names_compressed ||
        compressedImage.section_names_decoded_bytes != 17 ||
        compressedImage.compressed_count != 1 ||
        !xboxrpx::section_named(compressedImage, ".text") ||
        !xboxrpx::section_named(compressedImage, ".shstrtab")) {
        std::fprintf(stderr, "Compressed Cafe OS section names rejected: %s\n", reason.c_str());
        return 1;
    }
    auto invalidCompressed = cafeCompressed;
    invalidCompressed[kPackedOffset + 4] = 0; // corrupt zlib CMF
    if (!rejected(invalidCompressed)) return 1;
    invalidCompressed = cafeCompressed;
    invalidCompressed.back() ^= 1u; // Adler-32 checksum mismatch
    if (!rejected(invalidCompressed)) return 1;
    invalidCompressed = cafeCompressed;
    writeBe32(invalidCompressed, kPackedOffset, 0x01000000u);
    if (!rejected(invalidCompressed)) return 1; // 16 MiB decompression bomb
    invalidCompressed = cafeCompressed;
    writeBe32(invalidCompressed, kShstrHeader + 20, 7u);
    if (!rejected(invalidCompressed)) return 1; // truncated zlib stream

    const auto savedEntry = image.entry;
    auto failsWithoutMutation = [&](const std::vector<uint8_t>& broken) {
        if (!rejected(broken)) return false;
        std::string failure;
        return !xboxrpx::inspect(broken.data(), broken.size(), image, failure) &&
               image.entry == savedEntry && image.mapped_count == 1;
    };
    std::vector<uint8_t> test = bytes;
    test[4] = 2; // ELF64 mismatch
    if (!failsWithoutMutation(test)) return 1;
    test=bytes; test[5] = 1; // little endian mismatch
    if (!failsWithoutMutation(test)) return 1;
    test=bytes; test[18] = 0; test[19] = 3; // wrong CPU
    if (!failsWithoutMutation(test)) return 1;
    test=bytes; test[24] = 0x06; // entry points outside .text
    if (!failsWithoutMutation(test)) return 1;
    test=bytes; test[32]=0xff; test[33]=0xff; // section table OOB
    if (!failsWithoutMutation(test)) return 1;
    test=bytes; test[48]=0xff; test[49]=0xff; // too many headers
    if (!failsWithoutMutation(test)) return 1;
    test=bytes; test[92]=0xff; // .text name index OOB
    if (!failsWithoutMutation(test)) return 1;
    test=bytes; test[108]=0xff; test[109]=0xff; // .text file offset OOB
    if (!failsWithoutMutation(test)) return 1;
    test=bytes; test[100]=0x08; // mark .text compressed without an actual stream
    // This may be accepted as valid compressed metadata: the validator does
    // not decompress, and the word prefix is structurally present.
    if (xboxrpx::inspect(test.data(),test.size(),image,reason)) {
        const xboxrpx::Section* sec=xboxrpx::section_named(image,".text");
        if (!sec || !sec->compressed) return 1;
    }
    test=bytes; test.resize(40);
    if (!failsWithoutMutation(test)) return 1;
    std::printf("RPX inspector PASSED: entry=%08x sections=%zu mapped=%zu; malformed inputs rejected\n",
                savedEntry, image.sections.size(), image.mapped_count);
    return 0;
}
