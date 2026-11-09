// SPDX-License-Identifier: MIT
#include "backend_probe.h"
#include <cstring>
#include <stdexcept>
namespace gfxd3d12 {
namespace {
ProbeHost g_host{};
float g_color[4]{0.0f, 0.0f, 0.0f, 1.0f};
bool g_has_color = false;
bool g_has_quad = false;
uint64_t g_frames = 0;
void clear_color(const uint32_t* regs, uint32_t cb, const float rgba[4]) {
    if (!g_host.present) throw std::logic_error("Xbox D3D12 probe: no host attached");
    if (regs != nullptr || cb != kProbeColorTarget || !rgba)
        throw std::invalid_argument("Xbox D3D12 probe: real GX2 color target unsupported");
    std::memcpy(g_color, rgba, sizeof(g_color));
    g_has_color = true;
}
void draw(const uint32_t* regs, uint32_t prim, uint32_t count,
          uint32_t indexType, uint32_t indexAddr,
          uint32_t baseVertex, uint32_t instances) {
    if (!g_host.present) throw std::logic_error("Xbox D3D12 probe: no host attached");
    if (regs || prim != kProbePrimitive || count != 6 ||
        indexType != 0 || indexAddr != 0 || baseVertex != 0 ||
        instances != 1 || g_has_quad)
        throw std::invalid_argument("Xbox D3D12 probe: guest GX2 draw unsupported");
    g_has_quad = true;
}
void swap() {
    if (!g_host.present) throw std::logic_error("Xbox D3D12 probe: no host attached");
    if (!g_has_color) throw std::logic_error("Xbox D3D12 probe: swap without clear_color");
    g_host.present(g_host.context, g_color, g_has_quad);
    ++g_frames; // Present and GPU fence complete before callback returns.
    g_has_color = false;
    g_has_quad = false;
}
uint64_t count() { return g_frames; }
}
void attach_probe_host(ProbeHost host) {
    if (!host.context || !host.present) throw std::invalid_argument("Xbox D3D12 probe: invalid host");
    if (g_host.present) throw std::logic_error("Xbox D3D12 probe: already attached");
    g_host = host;
    g_frames = 0;
    g_has_color = false;
    g_has_quad = false;
}
void detach_probe_host() {
    g_host = {};
    g_frames = 0;
    g_has_color = false;
    g_has_quad = false;
}
uint64_t probe_frame_count() { return g_frames; }
const render::Backend& probe_backend() {
    static const render::Backend b = [] {
        render::Backend v{};
        v.api = render::Api::D3D12;
        v.clear_color = clear_color;
        v.draw = draw;
        v.swap = swap;
        v.frame_count = count;
        v.frames_completed = count;
        v.device = [] { return std::string("Xbox UWP D3D12 probe (partial)"); };
        return v;
    }();
    return b;
}
}
