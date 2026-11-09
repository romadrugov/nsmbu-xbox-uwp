#pragma once
// Explicit sparse guest memory reservation for Xbox UWP.
// Reserves fixed 4 GiB virtual address space but commits only requested
// 64-KiB slices. Does not provide automatic fault-driven lazy commit yet.
#include <cstddef>
#include <cstdint>

namespace xboxguest {
class GuestVm final {
public:
    GuestVm() = default;
    ~GuestVm();
    GuestVm(const GuestVm&) = delete;
    GuestVm& operator=(const GuestVm&) = delete;
    bool open();
    bool commit(uint32_t guestAddress, size_t length);
    void close();
    bool active() const { return base_ != nullptr; }
    bool write_watch() const { return writeWatch_; }
    uint32_t last_error() const { return lastError_; }
    unsigned char* ptr(uint32_t guestAddress) const {
        return base_ ? base_ + guestAddress : nullptr;
    }
private:
    unsigned char* base_ = nullptr;
    bool writeWatch_ = false;
    uint32_t lastError_ = 0;
};
} // namespace xboxguest
