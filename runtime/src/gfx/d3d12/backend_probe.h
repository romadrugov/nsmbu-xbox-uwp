#pragma once
// SPDX-License-Identifier: MIT
// Partial Xbox integration with the actual render::Backend command interface.
// Synthetic test calls only. No guest GX2 shader/register handling.
#include <cstdint>
#include "gfx/renderer.h"
namespace gfxd3d12 {
inline constexpr uint32_t kProbeColorTarget = 0x58424f58u; // XBOX
inline constexpr uint32_t kProbePrimitive = 0x51554144u;   // QUAD
struct ProbeHost {
    void* context = nullptr;
    void (*present)(void*, const float rgba[4], bool draw_quad) = nullptr;
};
void attach_probe_host(ProbeHost host);
void detach_probe_host();
uint64_t probe_frame_count();
// Real render::Backend ABI, with only clear/draw/swap/frame counters/device set.
// Remaining entries null; do not use as a playable GX2 renderer.
const render::Backend& probe_backend();
}
