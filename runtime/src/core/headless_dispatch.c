// Native x64 public-fixture dispatcher, adapted from the table structure in
// core/core.cpp. Full game HLE registration, threading, and loader absent.
#include "core/headless_dispatch.h"
#include "recomp_table.h"
#include <stdlib.h>

#define GUEST_TEXT_BEGIN 0x02000000u
#define GUEST_TEXT_SIZE  0x01000000u
#define GUEST_SLOT_BEGIN 0xC0000000u
#define GUEST_SLOT_SIZE  0x00040000u
#define GUEST_HOST_BEGIN 0xC2000000u
#define GUEST_HOST_SIZE  0x00080000u
#define GUEST_HOST_STRIDE 8u

static PpcFunc* s_text;
static PpcFunc* s_slots;
static PpcFunc* s_hosts;
static uint32_t s_next_host;

static int set_fn(uint32_t addr, PpcFunc fn) {
    if (!fn) return 0;
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
    return 0;
}

void headless_dispatch_reset(void) {
    free(s_text);
    free(s_slots);
    free(s_hosts);
    s_text = 0;
    s_slots = 0;
    s_hosts = 0;
    s_next_host = GUEST_HOST_BEGIN;
}

int headless_dispatch_init(void) {
    headless_dispatch_reset();
    s_text = (PpcFunc*)calloc(GUEST_TEXT_SIZE / 4u, sizeof(PpcFunc));
    s_slots = (PpcFunc*)calloc(GUEST_SLOT_SIZE / 4u, sizeof(PpcFunc));
    s_hosts = (PpcFunc*)calloc(GUEST_HOST_SIZE / GUEST_HOST_STRIDE, sizeof(PpcFunc));
    if (!s_text || !s_slots || !s_hosts) {
        headless_dispatch_reset();
        return 0;
    }
    for (unsigned i=0; i<g_recomp_func_count; ++i) {
        if (!set_fn(g_recomp_funcs[i].addr, g_recomp_funcs[i].fn)) {
            headless_dispatch_reset();
            return 0;
        }
    }
    for (unsigned i=0; i<g_recomp_import_count; ++i) {
        if (g_recomp_imports[i].is_func &&
            !set_fn(g_recomp_imports[i].slot, g_recomp_imports[i].fn)) {
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
    return 0;
}

uint32_t headless_dispatch_register_host(PpcFunc fn) {
    if (!s_hosts || !fn ||
        s_next_host - GUEST_HOST_BEGIN >= GUEST_HOST_SIZE) return 0;
    uint32_t address = s_next_host;
    s_next_host += GUEST_HOST_STRIDE;
    if (!set_fn(address, fn)) return 0;
    return address;
}

// Needed for indirect calls emitted by the actual PowerPC-to-C translator.
// The function signature matches the real runtime's musttail dispatcher.
void ppc_dispatch(Cpu* cpu) {
    PpcFunc fn = headless_dispatch_lookup(cpu->pc);
    if (!fn) {
        ppc_unimplemented(cpu, cpu->pc, 0);
        return;
    }
    MUSTTAIL return fn(cpu);
}
