#include "core/rpx_inspect.h"
#include "core/rpx_stage.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
void put16(std::vector<uint8_t>& b, size_t pos, uint16_t v) {
    b[pos] = uint8_t(v >> 8); b[pos+1] = uint8_t(v);
}
void put32(std::vector<uint8_t>& b, size_t pos, uint32_t v) {
    b[pos] = uint8_t(v >> 24); b[pos+1] = uint8_t(v >> 16);
    b[pos+2] = uint8_t(v >> 8); b[pos+3] = uint8_t(v);
}
std::vector<uint8_t> make_rpx() {
    constexpr size_t kShOff=52, kNum=6;
    std::vector<uint8_t> b(kShOff + kNum*40, 0);
    b[0]=0x7f; b[1]='E'; b[2]='L'; b[3]='F';
    b[4]=1; b[5]=2; b[6]=1; b[7]=0xca;
    put16(b,16,0xFE01); put16(b,18,20); put32(b,20,1);
    put32(b,24,0x02000000u);
    put32(b,32, uint32_t(kShOff));
    put16(b,40,52); put16(b,46,40); put16(b,48,kNum); put16(b,50,5);

    const uint8_t compressedText[] = {
        0,0,0,12,
        0x78,0xDA,0xB3,0x48,0x60,0xD0,0xB0,0x48,0x66,0x60,
        0xF2,0x6B,0x60,0x50,0,0,0x0F,0x8B,0x02,0x4C
    };
    const uint8_t rodata[] = { 0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,
                               0x39,0x30,0x41,0x42,0x43,0x44,0x45,0x46 };
    const uint8_t data[] = { 0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x10,
                             0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF };
    const char nameArray[] = "\0.text\0.rodata\0.data\0.bss\0.shstrtab\0";
    const std::string names(nameArray, sizeof(nameArray));
    const uint32_t textOff=uint32_t(b.size());
    b.insert(b.end(),compressedText,compressedText+sizeof(compressedText));
    const uint32_t roOff=uint32_t(b.size());
    b.insert(b.end(),rodata,rodata+sizeof(rodata));
    const uint32_t dataOff=uint32_t(b.size());
    b.insert(b.end(),data,data+sizeof(data));
    const uint32_t namesOff=uint32_t(b.size());
    b.insert(b.end(),names.begin(),names.end());

    auto section=[&](unsigned index,const char* name,uint32_t type,
                     uint32_t flags,uint32_t address,uint32_t off,uint32_t size) {
        const size_t i = kShOff + size_t(index)*40;
        put32(b,i, uint32_t(names.find(name)));
        put32(b,i+4,type); put32(b,i+8,flags);
        put32(b,i+12,address); put32(b,i+16,off);
        put32(b,i+20,size); put32(b,i+32,4);
    };
    section(1,".text",1,0x08000006u,0x02000000u,textOff,sizeof(compressedText));
    section(2,".rodata",1,2u,0x03000000u,roOff,sizeof(rodata));
    section(3,".data",1,3u,0x10000000u,dataOff,sizeof(data));
    section(4,".bss",8,3u,0x10020000u,0,64);
    section(5,".shstrtab",3,0u,0u,namesOff,uint32_t(names.size()));
    return b;
}
bool fail(const char* what) {
    std::fprintf(stderr,"RPX staging fixture FAILED: %s\n",what);
    return false;
}
}

int main() {
    const auto bytes=make_rpx();
    xboxrpx::Image image;
    std::string error;
    if (!xboxrpx::inspect(bytes.data(),bytes.size(),image,error)) {
        std::fprintf(stderr,"RPX synthetic ELF rejected: %s\n",error.c_str());
        return 1;
    }
    std::vector<xboxrpx::PreparedSection> prepared;
    if (!xboxrpx::prepare_sections(bytes.data(),bytes.size(),image,prepared,error)) {
        std::fprintf(stderr,"RPX synthetic sections rejected: %s\n",error.c_str());
        return 1;
    }
    if (prepared.size()!=4) return fail("expected text/rodata/data/bss") ? 0:1;
    const uint8_t originalText[]={
        0x38,0x60,0x00,0x28,0x38,0x63,0x00,0x02,0x4E,0x80,0x00,0x20
    };
    if (prepared[0].address!=0x02000000u ||
        prepared[0].bytes.size()!=12 ||
        std::memcmp(prepared[0].bytes.data(),originalText,12)!=0 ||
        prepared[0].crc32!=0x372B6980u)
        return fail("compressed .text mismatch") ? 0:1;
    if (prepared[1].name!=".rodata" || prepared[1].bytes.size()!=16 ||
        prepared[2].name!=".data" || prepared[2].bytes.size()!=16)
        return fail("raw rodata/data mismatch") ? 0:1;
    if (prepared[3].name!=".bss" || !prepared[3].zero_fill ||
        prepared[3].bytes.size()!=64 ||
        !std::all_of(prepared[3].bytes.begin(),prepared[3].bytes.end(),
                     [](uint8_t b){return b==0;}) ||
        prepared[3].crc32!=0x758D6336u)
        return fail(".bss must be zero-filled") ? 0:1;

    // R5E opt-in must stage only two known boot metadata/data sections.
    // All bytes below reuse this hand-authored synthetic fixture.
    auto withBoot = image;
    xboxrpx::Section syscall{};
    syscall.name = ".syscall";
    syscall.type = 1u;
    syscall.flags = 6u;
    syscall.address = 0x02000100u; // distinct from .text
    syscall.file_offset = image.sections[2].file_offset;
    syscall.file_bytes = 8u;
    syscall.memory_bytes = 8u;
    withBoot.sections.push_back(syscall);
    xboxrpx::Section module{};
    module.name = ".module_id";
    module.type = 1u;
    module.flags = 3u;
    module.address = 0x10018000u; // inside synthetic MEM2 gap
    module.file_offset = image.sections[3].file_offset;
    module.file_bytes = 8u;
    module.memory_bytes = 8u;
    withBoot.sections.push_back(module);
    std::vector<xboxrpx::PreparedSection> bootPrepared;
    if (!xboxrpx::prepare_sections(bytes.data(),bytes.size(),withBoot,
                                    bootPrepared,error,true) ||
        bootPrepared.size()!=6 ||
        bootPrepared[4].name!=".syscall" ||
        bootPrepared[5].name!=".module_id")
        return fail("R5E exact boot-section opt-in failed") ? 0:1;
    uint32_t mem2Start=0,mem2Bytes=0;
    if (!xboxrpx::boot_mem2_span(bootPrepared,mem2Start,mem2Bytes) ||
        mem2Start!=0x10000000u || mem2Bytes!=0x20040u)
        return fail("R5E contiguous MEM2 span mismatch") ? 0:1;
    if (!xboxrpx::prepare_sections(bytes.data(),bytes.size(),withBoot,
                                    bootPrepared,error) || bootPrepared.size()!=4)
        return fail("R5E default smoke must remain 4 sections") ? 0:1;
    auto hugeSpan=bootPrepared;
    hugeSpan.push_back(bootPrepared[0]);
    hugeSpan.back().address=0x20000000u;
    if (xboxrpx::boot_mem2_span(hugeSpan,mem2Start,mem2Bytes))
        return fail("R5E unbounded MEM2 accepted") ? 0:1;

    // A bad section cannot replace previously prepared guest data.
    const auto goodCrc = prepared[0].crc32;
    auto overlap = image;
    overlap.sections[2].address = 0x02000008u;
    if (xboxrpx::prepare_sections(bytes.data(),bytes.size(),overlap,prepared,error) ||
        prepared[0].crc32!=goodCrc)
        return fail("overlap should fail atomically") ? 0:1;
    auto badStream = bytes;
    badStream[52 + 6*40 + 23] ^= 1; // last Adler-32 byte
    if (xboxrpx::prepare_sections(badStream.data(),badStream.size(),image,prepared,error) ||
        prepared[0].crc32!=goodCrc)
        return fail("corrupted zlib stream must fail") ? 0:1;
    auto overBudget = image;
    overBudget.sections[1].memory_bytes = 40u*1024u*1024u;
    if (xboxrpx::prepare_sections(bytes.data(),bytes.size(),overBudget,prepared,error))
        return fail("over-budget .text accepted") ? 0:1;
    auto lostText = image;
    lostText.sections[1].name=".missing";
    if (xboxrpx::prepare_sections(bytes.data(),bytes.size(),lostText,prepared,error))
        return fail("missing text accepted") ? 0:1;
    std::puts("RPX staging PASSED: Cafe OS compressed .text, raw .rodata/.data, "
              "NOBITS .bss, CRC32, overlap, checksum and budget rejection");
    return 0;
}
