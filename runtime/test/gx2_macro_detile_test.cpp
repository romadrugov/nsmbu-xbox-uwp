// Independent, fixed GX2 2D Thin1 address fixtures for an uncompressed
// RGBA8 surface. Tile base vectors taken from Cemu LatteAddrLib_Coord.cpp,
// not from the production macro address function under test.
#include "gfx/d3d12/gx2_macro_detile.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
constexpr uint8_t kPixelOrder[64] = {
     0, 1, 2, 3, 8, 9,10,11,
     4, 5, 6, 7,12,13,14,15,
    16,17,18,19,24,25,26,27,
    20,21,22,23,28,29,30,31,
    32,33,34,35,40,41,42,43,
    36,37,38,39,44,45,46,47,
    48,49,50,51,56,57,58,59,
    52,53,54,55,60,61,62,63
};
// [swizzle case][macro Y=0..2][microtile Y=0..1][microtile X=0..3]
// Byte offsets are within a 2048-byte macro-tile.
constexpr uint16_t kBases[2][3][2][4] = {
  { // GX2 swizzle 0: 2 pipes, 4 banks
    {{0,768,1024,1792}, {256,512,1280,1536}},
    {{1024,1792,0,768}, {1280,1536,256,512}},
    {{512,256,1536,1280}, {768,0,1792,1024}}
  },
  { // GX2 swizzle 0x500: pipe=1, bank=2
    {{1280,1536,256,512}, {1024,1792,0,768}},
    {{256,512,1280,1536}, {0,768,1024,1792}},
    {{1792,1024,768,0}, {1536,1280,512,256}}
  }
};

bool run_case(unsigned table, uint32_t swizzle) {
    constexpr uint32_t w=53, h=37, pitch=64, paddedH=48;
    std::vector<uint8_t> encoded(size_t(pitch)*paddedH*4, 0xA5u);
    std::vector<uint8_t> expected(size_t(w)*h*4);
    // Encode entirely from fixed reference tile-base and pixel-order tables,
    // without calling the production address calculator.
    for (uint32_t y=0; y<h; ++y) {
        for (uint32_t x=0; x<w; ++x) {
            const uint8_t px[4] = {uint8_t(x*7+y), uint8_t(y*11+x),
                                   uint8_t((x*13)^(y*17)), 255};
            const size_t visible = (size_t(y)*w+x)*4;
            std::memcpy(expected.data()+visible, px, 4);
            const size_t macro = size_t(y/16)*(pitch/32) + x/32;
            const size_t base = kBases[table][y/16][(y%16)/8][(x%32)/8];
            const size_t elem = kPixelOrder[(y%8)*8+(x%8)]*4u;
            const size_t address = macro*2048 + base + elem;
            if (address+4 > encoded.size()) return false;
            std::memcpy(encoded.data()+address, px, 4);
        }
    }
    std::vector<uint8_t> result(4, 0x5A);
    if (!gfxd3d12::detile_macro2d_thin1_rgba8(
        encoded.data(),encoded.size(),w,h,pitch,swizzle,result)) return false;
    if (result != expected) return false;

    // Reject invalid input without mutating the previous good result.
    const auto saved = result;
    if (gfxd3d12::detile_macro2d_thin1_rgba8(
        encoded.data(),encoded.size()-1,w,h,pitch,swizzle,result)) return false;
    if (gfxd3d12::detile_macro2d_thin1_rgba8(
        encoded.data(),encoded.size(),w,h,63,swizzle,result)) return false;
    if (gfxd3d12::detile_macro2d_thin1_rgba8(
        encoded.data(),encoded.size(),65,h,pitch,swizzle,result)) return false;
    if (gfxd3d12::detile_macro2d_thin1_rgba8(
        nullptr,encoded.size(),w,h,pitch,swizzle,result)) return false;
    return result == saved;
}

} // namespace

int main() {
    const bool ok = run_case(0,0) && run_case(1,0x500) &&
                    run_case(1,0xD0500);
    std::puts(ok ? "GX2 macro 2D Thin1 reference vectors PASSED"
                 : "GX2 macro 2D Thin1 reference vectors FAILED");
    return ok ? 0 : 1;
}
