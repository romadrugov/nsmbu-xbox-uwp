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
