// Native x64 public-fixture dispatcher, adapted from core/core.cpp.
// The real desktop dispatcher has a fallback "other" address map in addition
// to dense text, import slots and HLE host ranges. The original R4 probe
// incorrectly rejected every address outside those dense windows, causing
// the Xbox diagnostic table initialization to fail before entry lookup.
//
// This file stores native C function pointers only. It neither stages
// Nintendo RPX memory nor executes guest code during initialization.
#include "core/headless_dispatch.h"
#include "recomp_table.h"
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#define GUEST_TEXT_BEGIN 0x02000000u
#define GUEST_TEXT_SIZE  0x01000000u
#define GUEST_SLOT_BEGIN 0xC0000000u
#define GUEST_SLOT_SIZE  0x00040000u
#define GUEST_HOST_BEGIN 0xC2000000u
#define GUEST_HOST_SIZE  0x00080000u
#define GUEST_HOST_STRIDE 8u

typedef struct {
    uint32_t address;
    PpcFunc func;
    unsigned char occupied;
} OtherEntry;

static PpcFunc* s_text;
static PpcFunc* s_slots;
static PpcFunc* s_hosts;
static OtherEntry* s_other;
static size_t s_other_capacity;
static uint32_t s_next_host;
static HeadlessDispatchDiagnostic s_diagnostic;

static size_t other_hash(uint32_t key) {
    key ^= key >> 16;
    key *= 0x7feb352du;
    key ^= key >> 15;
    key *= 0x846ca68bu;
    key ^= key >> 16;
    return (size_t)key;
}

// Open-addressed table with <= 50% initial load, including replacements.
// Used only for addresses that core/core.cpp stores in dispatch::g_other.
static int set_other(uint32_t addr, PpcFunc fn) {
    if (!s_other || s_other_capacity == 0) return 0;
    const size_t mask = s_other_capacity - 1u;
    size_t slot = other_hash(addr) & mask;
    for (size_t probe = 0; probe < s_other_capacity; ++probe) {
        OtherEntry* e = &s_other[slot];
        if (!e->occupied) {
            e->address = addr;
            e->func = fn;
            e->occupied = 1;
            s_diagnostic.fallback++;
            return 1;
        }
        if (e->address == addr) {
            e->func = fn;
            return 1;
        }
        slot = (slot + 1u) & mask;
    }
    return 0;
}

static PpcFunc get_other(uint32_t addr) {
    if (!s_other || s_other_capacity == 0) return 0;
    const size_t mask = s_other_capacity - 1u;
    size_t slot = other_hash(addr) & mask;
    for (size_t probe = 0; probe < s_other_capacity; ++probe) {
        const OtherEntry* e = &s_other[slot];
        if (!e->occupied) return 0;
        if (e->address == addr) return e->func;
        slot = (slot + 1u) & mask;
    }
    return 0;
}

static int set_fn(uint32_t addr, PpcFunc fn) {
    // Upstream dispatch::set accepts null function targets. They remain
    // unresolvable at execution time, instead of crashing initialization.
    if (!fn) {
        s_diagnostic.null_targets++;
        return 1;
    }
    if (addr >= GUEST_TEXT_BEGIN && addr - GUEST_TEXT_BEGIN < GUEST_TEXT_SIZE &&
        !(addr & 3u)) {
        s_text[(addr - GUEST_TEXT_BEGIN) >> 2] = fn;
        return 1;
    }
    if (addr >= GUEST_SLOT_BEGIN && addr - GUEST_SLOT_BEGIN < GUEST_SLOT_SIZE &&
        !(addr & 3u)) {
        s_slots[(addr - GUEST_SLOT_BEGIN) >> 2] = fn;
        return 1;
    }
    if (addr >= GUEST_HOST_BEGIN && addr - GUEST_HOST_BEGIN < GUEST_HOST_SIZE &&
        ((addr - GUEST_HOST_BEGIN) % GUEST_HOST_STRIDE) == 0) {
        s_hosts[(addr - GUEST_HOST_BEGIN) / GUEST_HOST_STRIDE] = fn;
        return 1;
    }
    return set_other(addr, fn);
}

void headless_dispatch_reset(void) {
    free(s_text);
    free(s_slots);
    free(s_hosts);
    free(s_other);
    s_text = 0;
    s_slots = 0;
    s_hosts = 0;
    s_other = 0;
    s_other_capacity = 0;
    s_next_host = GUEST_HOST_BEGIN;
    // Keep the diagnostic snapshot until the next initialization.
}

void headless_dispatch_diagnostic(HeadlessDispatchDiagnostic* result) {
    if (result) *result = s_diagnostic;
}

int headless_dispatch_init(void) {
    headless_dispatch_reset();
    memset(&s_diagnostic, 0, sizeof(s_diagnostic));
    // Bound allocations even if caller gives unexpected metadata.
    const uint64_t total = (uint64_t)g_recomp_func_count +
                           (uint64_t)g_recomp_import_count;
    if (total > 500000u) {
        s_diagnostic.failure = 1;
        return 0;
    }
    size_t cap = 16u;
    while (cap < (size_t)(total * 2u + 1u)) cap <<= 1;
    s_text = (PpcFunc*)calloc(GUEST_TEXT_SIZE / 4u, sizeof(PpcFunc));
    s_slots = (PpcFunc*)calloc(GUEST_SLOT_SIZE / 4u, sizeof(PpcFunc));
    s_hosts = (PpcFunc*)calloc(GUEST_HOST_SIZE / GUEST_HOST_STRIDE, sizeof(PpcFunc));
    s_other = (OtherEntry*)calloc(cap, sizeof(OtherEntry));
    s_other_capacity = cap;
    if (!s_text || !s_slots || !s_hosts || !s_other) {
        s_diagnostic.failure = 1;
        headless_dispatch_reset();
        return 0;
    }
    for (uint32_t i = 0; i < g_recomp_func_count; ++i) {
        if (!set_fn(g_recomp_funcs[i].addr, g_recomp_funcs[i].fn)) {
            s_diagnostic.failure = 2;
            s_diagnostic.source = 1;
            s_diagnostic.index = i;
            s_diagnostic.address = g_recomp_funcs[i].addr;
            headless_dispatch_reset();
            return 0;
        }
    }
    for (uint32_t i = 0; i < g_recomp_import_count; ++i) {
        if (g_recomp_imports[i].is_func &&
            !set_fn(g_recomp_imports[i].slot, g_recomp_imports[i].fn)) {
            s_diagnostic.failure = 2;
            s_diagnostic.source = 2;
            s_diagnostic.index = i;
            s_diagnostic.address = g_recomp_imports[i].slot;
            headless_dispatch_reset();
            return 0;
        }
    }
    return 1;
}

PpcFunc headless_dispatch_lookup(uint32_t addr) {
    if (!s_text) return 0;
    if (addr >= GUEST_TEXT_BEGIN && addr - GUEST_TEXT_BEGIN < GUEST_TEXT_SIZE &&
        !(addr & 3u)) return s_text[(addr - GUEST_TEXT_BEGIN) >> 2];
    if (addr >= GUEST_SLOT_BEGIN && addr - GUEST_SLOT_BEGIN < GUEST_SLOT_SIZE &&
        !(addr & 3u)) return s_slots[(addr - GUEST_SLOT_BEGIN) >> 2];
    if (addr >= GUEST_HOST_BEGIN && addr - GUEST_HOST_BEGIN < GUEST_HOST_SIZE &&
        ((addr - GUEST_HOST_BEGIN) % GUEST_HOST_STRIDE) == 0)
        return s_hosts[(addr - GUEST_HOST_BEGIN) / GUEST_HOST_STRIDE];
    return get_other(addr);
}

uint32_t headless_dispatch_register_host(PpcFunc fn) {
    if (!s_hosts || !fn ||
        s_next_host - GUEST_HOST_BEGIN >= GUEST_HOST_SIZE) return 0;
    uint32_t address = s_next_host;
    s_next_host += GUEST_HOST_STRIDE;
    if (!set_fn(address, fn)) return 0;
    return address;
}

// Needed for indirect calls emitted by PowerPC-to-C translator.
// ABI and tail-call behavior match the upstream native runtime.
void ppc_dispatch(Cpu* cpu) {
    PpcFunc fn = headless_dispatch_lookup(cpu->pc);
    if (!fn) {
        ppc_unimplemented(cpu, cpu->pc, 0);
        return;
    }
    MUSTTAIL return fn(cpu);
}
