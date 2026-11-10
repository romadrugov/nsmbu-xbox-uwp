// Executes unchanged upstream coreinit_mem HLE against an isolated
// author-created guest memory region in a Windows desktop CI process.
// This tests behavior; it is NOT a UWP activation or console test.
#include <windows.h>
#include <cstdint>
#include <cstdio>

extern "C" int nsmbu_runtime_hle_link_probe();
extern "C" int nsmbu_runtime_hle_heap_roundtrip(uint32_t guest_start);

int main() {
    int rc = nsmbu_runtime_hle_link_probe();
    if (rc != 0) {
        std::printf("HLE basic/registry FAIL: %d\n", rc);
        return rc;
    }
    constexpr uintptr_t kGuestBase = 0x200000000000ull;
    constexpr uint32_t kGuestStart = 0x10010000u;
    void* expected = reinterpret_cast<void*>(kGuestBase + kGuestStart);
    void* region = VirtualAlloc(expected, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (region != expected) {
        if (region) VirtualFree(region, 0, MEM_RELEASE);
        std::printf("Could not reserve exact synthetic PPC guest range (winerr=%lu)\n", GetLastError());
        return 20;
    }
    rc = nsmbu_runtime_hle_heap_roundtrip(kGuestStart);
    if (!VirtualFree(region, 0, MEM_RELEASE)) return 21;
    if (rc != 0) {
        std::printf("Cafe OS heap HLE FAIL: %d\n", rc);
        return rc;
    }
    std::puts("PASS: real upstream Cafe OS HLE registration, heap create/alloc/free, guest endian header, and MSVC/Clang ABI");
    return 0;
}
