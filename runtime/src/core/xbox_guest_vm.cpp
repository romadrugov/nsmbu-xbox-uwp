#include "xbox_guest_vm.h"
#include <windows.h>

namespace xboxguest {
namespace {
constexpr SIZE_T kFourGiB = static_cast<SIZE_T>(0x100000000ull);
constexpr uint64_t kCommitChunk = 65536ull;
constexpr uintptr_t kPpcFixedBase = static_cast<uintptr_t>(0x200000000000ull);
}

GuestVm::~GuestVm() { close(); }

bool GuestVm::open() {
    close();
    auto* const requested = reinterpret_cast<void*>(kPpcFixedBase);
    void* p = VirtualAllocFromApp(requested, kFourGiB,
                                 MEM_RESERVE | MEM_WRITE_WATCH, PAGE_NOACCESS);
    writeWatch_ = p != nullptr;
    if (!p) {
        p = VirtualAllocFromApp(requested, kFourGiB, MEM_RESERVE, PAGE_NOACCESS);
    }
    if (!p) {
        lastError_ = static_cast<uint32_t>(GetLastError());
        return false;
    }
    if (p != requested) {
        VirtualFree(p, 0, MEM_RELEASE);
        lastError_ = ERROR_INVALID_ADDRESS;
        return false;
    }
    base_ = static_cast<unsigned char*>(p);
    lastError_ = 0;
    return true;
}

bool GuestVm::commit(uint32_t guestAddress, size_t length) {
    if (!base_ || length == 0) return false;
    const uint64_t start = static_cast<uint64_t>(guestAddress);
    const uint64_t end = start + static_cast<uint64_t>(length);
    // Keep the guest null page inaccessible. Reject overflow/wraparound.
    if (start < kCommitChunk || end > kFourGiB || end <= start) return false;
    const uint64_t alignedStart = start & ~(kCommitChunk - 1u);
    const uint64_t alignedEnd = (end + kCommitChunk - 1u) & ~(kCommitChunk - 1u);
    if (alignedEnd > kFourGiB) return false;
    void* const requested = base_ + static_cast<size_t>(alignedStart);
    void* p = VirtualAllocFromApp(requested,
                                 static_cast<SIZE_T>(alignedEnd-alignedStart),
                                 MEM_COMMIT, PAGE_READWRITE);
    if (p != requested) {
        lastError_ = static_cast<uint32_t>(GetLastError());
        return false;
    }
    lastError_ = 0;
    return true;
}

void GuestVm::close() {
    if (base_) VirtualFree(base_, 0, MEM_RELEASE);
    base_ = nullptr;
    writeWatch_ = false;
    lastError_ = 0;
}
} // namespace xboxguest
