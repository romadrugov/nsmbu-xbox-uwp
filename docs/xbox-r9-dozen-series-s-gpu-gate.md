# R9 — first *real* Mesa Dozen/Vulkan frame on Xbox UWP

## Why this integration exists

R8 #4 successfully compiled and **linked** the complete desktop
`nsmbu.exe` with owner-verified USA_V0 recompiled code. It did **not**
run the game and is **not** an Xbox UWP binary. Its binaries used a temporary
entry-hook ABI adapter that does not reconstruct instruction-level hooks.

The existing Xbox R7D 0.9.3.0 package is a separate proven fallback; it must
not be replaced by an unverified full-runtime experiment.

The first crucial Xbox gap is **Vulkan on Series S**. Based on inspected source
from `jajdp/smash4-xbox@50ed2e91`, its UWP patch allows Mesa Dozen to render
through a WinRT CoreWindow, using Direct3D12 in the Xbox driver. Smash4's
author reported an actual result on Series X, not Series S.

## What the R9 code really does

A *new* package identity `Romadrugov.NSMBUXboxDozenR9` runs a separate UWP
CoreApplication on the actual owning CoreWindow thread. Its
`DozenFrame.cpp` performs, with the packaged Dozen ICD:

1. `LoadPackagedLibrary("vulkan_dzn.dll")`, retrieve the
   `vk_icdGetInstanceProcAddr` entry (no desktop `vulkan-1.dll`).
2. Enumerate instance extensions and request Vulkan 1.2, including Win32 WSI.
3. Enumerate the **real** GPU and extensions; log API and storage-buffer limit.
4. Create a genuine `VkDevice` graphics queue and `VkWin32SurfaceKHR`
   whose handle is the WinRT `CoreWindow` pointer, as expected by the
   patched Mesa Windows WSI.
5. Create `VK_KHR_swapchain`, acquire a backbuffer, clear it with an actual
   Vulkan command buffer, queue-submit, present, and wait for a fence.
6. Log each Vulkan error, and on success:
   `REAL VULKAN/DOZEN FRAME PRESENT PASS`.

That clear-color frame is **synthetic**. It does NOT run any Nintendo game
functions, GX2 commands, level resources, or game assets. It is a hardware
GPU compatibility gate for the full runtime.

## Build sources and reproducibility

- Official Mesa release: `mesa-26.2.6` from
  `https://gitlab.freedesktop.org/mesa/mesa.git`
- Smash4 Xbox scripts/patch:
  `jajdp/smash4-xbox@50ed2e91d9ae0aadfac5cf48777c8a825c5231e9`
- Vulkan-Headers:
  `KhronosGroup/Vulkan-Headers@88487f687f3100ebe1d3559a8a34d971ee85b977`
- Public Windows CI builds an empty-driver signed UWP shell; this **does
  not** imply Dozen compatibility. The separate public CI builds Mesa,
  packages the actual driver with `dxil.dll`, signs the R9 AppX and
  uploads it as a game-free GitHub artifact once all steps succeed.

All game-derived code stays in the owner's private R8 workflows.
No game code is checked into this public branch or packaged in R9 probe.

## Console hardware validation / user feedback

After a successful driver-and-AppX workflow, install
`NSMBUDozenProbe_R9_x64.appx` *alongside* the existing R7D. Also install
`Microsoft.VCLibs.x64.14.00.appx` if not installed and the associated
`NSMBUDozenProbe.cer` certificate if needed.

On Xbox Series S Dev Mode, read:
`LocalState/nsmbu_r9_dozen_gpu.txt`.
Only the line `REAL VULKAN/DOZEN FRAME PRESENT PASS` after
`SWAPCHAIN CREATED` confirms actual GPU presentation.

A missing DLL, vkCreateInstance failure, unsupported format, device
creation, swapchain, queue-submit or present error is a real integration
blocker to debug on hardware. Failure is not a reason to modify the
stabilized R7D package.

## Next major implementation (no fabricated progress)

Once Dozen works on Series S, replace the experimental R9 synthetic clear
with real NSMBU Recomp GX2 Vulkan draw commands. The full game UWP target
needs **AppContainer-compatible** game executable + SDL3, glslang,
Cemu Latte shaders, Cafe OS threads, guest-memory layout, VFS, and access
to the owner's complete `code/content/meta` dump in LocalState.

Do not merely wrap the desktop R8 `nsmbu.exe`: its Mingw/MSYS2 desktop
CRT and forbidden Win32 imports make it non-UWP. The full game must be
recompiled with the Windows Store UWP target and dependencies.

A **first real game frame** only counts when it comes from the game's own
GX2 commands through the preserved NSMBU Vulkan renderer and Dozen;
a Dozen clear-color image is still not Mario.
