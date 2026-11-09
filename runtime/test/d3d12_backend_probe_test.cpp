#include "gfx/d3d12/backend_probe.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace render { const Backend* g_backend = nullptr; }
namespace {
struct FakeGPU { uint64_t presents=0; bool quad=false; float color[4]{}; };
void present(void* ctx, const float rgba[4], bool quad) {
    auto& gpu=*static_cast<FakeGPU*>(ctx);
    ++gpu.presents;
    gpu.quad=quad;
    for (int i=0;i<4;++i) gpu.color[i]=rgba[i];
}
template<class T> bool rejects(T&& run) {
    try { run(); } catch (const std::exception&) { return true; }
    return false;
}
}
int main() {
    FakeGPU gpu;
    const auto& backend=gfxd3d12::probe_backend();
    render::g_backend=&backend;
    assert(backend.api==render::Api::D3D12);
    assert(backend.clear_color && backend.draw && backend.swap);
    assert(!backend.copy_surface && !backend.clear_depth_stencil);
    assert(!backend.copy_to_scan && !backend.invalidate && !backend.init);
    assert(backend.device()=="Xbox UWP D3D12 probe (partial)");
    const float teal[4]={0.05f,0.3f,0.35f,1.0f};
    assert(rejects([] {render::swap();}));
    gfxd3d12::attach_probe_host({&gpu,present});
    assert(rejects([] {render::swap();}));
    assert(rejects([&] {render::clear_color(nullptr,12u,teal);}));
    assert(rejects([] {render::draw(nullptr,4u,6u,0u,0u,0u,1u);}));
    assert(rejects([] {render::draw(nullptr,gfxd3d12::kProbePrimitive,3u,0u,0u,0u,1u);}));
    assert(rejects([] {render::draw(nullptr,gfxd3d12::kProbePrimitive,6u,0u,0u,0u,2u);}));
    render::clear_color(nullptr,gfxd3d12::kProbeColorTarget,teal);
    render::draw(nullptr,gfxd3d12::kProbePrimitive,6,0,0,0,1);
    assert(rejects([] {render::draw(nullptr,gfxd3d12::kProbePrimitive,6,0,0,0,1);}));
    render::swap();
    assert(gpu.presents==1 && gpu.quad);
    assert(std::fabs(gpu.color[1]-0.3f)<0.00001f);
    assert(backend.frame_count()==1 && backend.frames_completed()==1);
    assert(rejects([] {render::swap();}));
    const float green[4]={0,0.5f,0,1};
    render::clear_color(nullptr,gfxd3d12::kProbeColorTarget,green);
    render::swap();
    assert(gpu.presents==2 && !gpu.quad);
    gfxd3d12::detach_probe_host();
    assert(rejects([&] {render::clear_color(nullptr,gfxd3d12::kProbeColorTarget,teal);}));
    render::g_backend=nullptr;
    std::puts("render::Backend D3D12 probe dispatch PASSED (clear/draw/swap, rejection, frame counts)");
    return 0;
}
