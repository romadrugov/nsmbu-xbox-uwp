// UWP-safe, conservative write tracking for the isolated R2 AppContainer link.
// Unlike desktop write_watch.cpp, does not use vectored exception handlers,
// guard pages, VirtualProtect, or fault-driven dirty tracking.
//
// MEM_WRITE_WATCH must be enabled by the owner of the guest reservation
// (Xbox GuestVm); no VirtualAlloc/MEM_COMMIT occurs here. We do NOT reset
// Windows write-watch state because outstanding overlapping consumers could
// otherwise miss a write. Consequently, previously written pages can be
// reported dirty again: correctness over efficiency until a generation-aware
// tracker is integrated and device-tested. Never report clean on API failure.
#include <winapifamily.h>
#if !defined(WINAPI_FAMILY) || WINAPI_FAMILY != WINAPI_FAMILY_APP
#error This implementation is intentionally for WINAPI_FAMILY_APP only
#endif

#include "core/write_watch.h"
#include "ppc.h"
#include <windows.h>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <mutex>
#include <vector>

namespace wwatch {
namespace {
std::atomic<uint8_t*> g_base{nullptr};
std::atomic<uint64_t> g_size{0};
std::atomic<uint64_t> g_serial{1};
std::atomic<uint64_t> g_hints{0};
std::atomic<uint64_t> g_hint_events_since_stats{0};
std::atomic<uint64_t> g_hint_bytes{0};

size_t page_size() {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    return info.dwPageSize;
}

bool bounds(uint32_t addr, uint32_t n, uint32_t& first, uint32_t& pages) {
    const uint64_t total = g_size.load(std::memory_order_acquire);
    const size_t ps = page_size();
    if (!total || !n || ps == 0 || (ps & (ps - 1))) return false;
    const uint64_t end = std::min<uint64_t>((uint64_t)addr + n, total);
    if ((uint64_t)addr >= end) return false;
    first = (uint32_t)((uint64_t)addr / ps);
    const uint64_t endPage = (end + ps - 1) / ps;
    pages = (uint32_t)(endPage - first);
    return pages != 0;
}

// Query only the relevant page range. A truncated query, missing MEM_WRITE_WATCH
// support, or any OS error must conservatively mean 'dirty'.
bool scan(uint32_t addr, uint32_t n, std::vector<void*>& dirty, bool& reliable) {
    reliable = false;
    uint8_t* const base = g_base.load(std::memory_order_acquire);
    uint32_t first = 0, count = 0;
    if (!base || !bounds(addr, n, first, count)) return false;
    const size_t ps = page_size();
    const uint64_t start = (uint64_t)first * ps;
    const size_t length = (size_t)count * ps;
    // A limited scratch buffer avoids allocating millions of pointers.
    // GetWriteWatch returns an error if the buffer is too small; in that case
    // callers must treat the whole range as dirty, not empty.
    const size_t capacity = std::min<size_t>(count, 4096);
    dirty.assign(capacity, nullptr);
    ULONG_PTR entries = (ULONG_PTR)dirty.size();
    DWORD granularity = 0;
    const UINT result = GetWriteWatch(0, base + start, length,
                                     dirty.data(), &entries, &granularity);
    if (result != 0 || granularity != ps || entries > dirty.size()) {
        dirty.clear();
        return false;
    }
    dirty.resize((size_t)entries);
    reliable = true;
    return !dirty.empty();
}
}

bool init(uint8_t* base, uint64_t size) {
    // Only the guest owner reserves memory. Even with no OS write-watch,
    // the query fallback below keeps change detection conservative.
    if (!base || !size || size > 0x100000000ull || base != PPC_MEM_BASE) return false;
    g_size.store(size, std::memory_order_release);
    g_base.store(base, std::memory_order_release);
    return true;
}

bool active() { return g_base.load(std::memory_order_acquire) != nullptr; }

uint64_t arm(uint32_t, uint32_t) {
    // No reset: historical dirtiness can cause false positives by design.
    return g_serial.fetch_add(1, std::memory_order_acq_rel);
}

bool changed_since(uint32_t addr, uint32_t size, uint64_t) {
    if (g_hints.load(std::memory_order_acquire)) return true;
    std::vector<void*> dirty;
    bool reliable = false;
    const bool written = scan(addr, size, dirty, reliable);
    return !reliable || written;
}

bool written_since(uint32_t addr, uint32_t size, uint64_t stamp) {
    return changed_since(addr, size, stamp);
}

uint32_t collect_written_pages(uint32_t addr, uint32_t size, uint64_t, uint32_t* out, uint32_t cap) {
    // ULONG_MAX signals to the caller that a complete list is unavailable.
    // Never return an empty list on unsupported/untrackable regions.
    if (!out || !cap || g_hints.load(std::memory_order_acquire)) return 0xffffffffu;
    std::vector<void*> dirty;
    bool reliable = false;
    scan(addr, size, dirty, reliable);
    if (!reliable || dirty.size() > cap) return 0xffffffffu;
    const uint64_t ps = page_size();
    const uint64_t first = (uint64_t)addr / ps;
    const uintptr_t base = (uintptr_t)g_base.load(std::memory_order_acquire);
    for (size_t i = 0; i < dirty.size(); ++i) {
        const uintptr_t absolute = (uintptr_t)dirty[i];
        if (absolute < base || (absolute - base) / ps < first) return 0xffffffffu;
        out[i] = (uint32_t)(((absolute - base) / ps) - first);
    }
    return (uint32_t)dirty.size();
}

bool page_span(uint32_t addr, uint32_t size, uint32_t& shift, uint32_t& first, uint32_t& count) {
    if (!bounds(addr, size, first, count)) return false;
    size_t ps = page_size();
    shift = 0;
    while (ps > 1) { ++shift; ps >>= 1; }
    return true;
}

// No pages are protected with PAGE_READONLY, so every committed guest
// page remains readable. This is not a claim of address validity.
bool readable(uint32_t, uint32_t) { return true; }
uint64_t write_seq() { return g_serial.load(std::memory_order_acquire); }
void enable_hints() {}

void hint(uint32_t, uint32_t size) {
    g_hints.fetch_add(1, std::memory_order_acq_rel);
    g_hint_events_since_stats.fetch_add(1, std::memory_order_relaxed);
    g_hint_bytes.fetch_add(size, std::memory_order_relaxed);
}
void host_write_begin(uint32_t addr, uint32_t size) { hint(addr, size); }
void host_write_end(uint32_t addr, uint32_t size) { hint(addr, size); }

void take_stats(uint64_t& faults, uint64_t& protectedPages) {
    faults = 0;
    protectedPages = 0;
}
void take_hint_stats(uint64_t& calls, uint64_t& bytes) {
    // Keep global conservative hint history sticky across statistics resets.
    calls = g_hint_events_since_stats.exchange(0, std::memory_order_acq_rel);
    bytes = g_hint_bytes.exchange(0, std::memory_order_acq_rel);
}
uint64_t protect_failures() { return 0; }

} // namespace wwatch
