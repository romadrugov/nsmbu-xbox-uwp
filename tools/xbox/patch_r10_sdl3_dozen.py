#!/usr/bin/env python3
"""Patch the pinned SDL3-UWP WinRT video backend to use packaged Mesa Dozen.

This patches OPEN SOURCE SDL3 source in a private CI working tree. It does not
touch user game data and requires the known WinRT video-driver code anchors.
Unlike a Vulkan test-only clear, these callbacks are consumed by the REAL
NSMBU Recomp renderer's SDL_Vulkan_LoadLibrary/CreateSurface call chain.

Requires Mesa 26.2.6 with Smash4 Xbox's UWP CoreWindow WSI patch in the
AppX, and real game content + remaining WinRT runtime link before gameplay.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import sys

HEADER_ANCHOR = '#include "SDL_winrtvideo_cpp.h"\n'
CALLBACK_ANCHOR = '    device->GetWindowSizeInPixels = WINRT_GetWindowSizeInPixels;\n'
HEADER = '''\
#if defined(SDL_VIDEO_VULKAN)
#define VK_USE_PLATFORM_WIN32_KHR
extern "C" {
#include "../SDL_vulkan_internal.h"
}
#endif
'''
SHIM = r'''
#if defined(SDL_VIDEO_VULKAN)
// R10 UWP Vulkan bridge. Existing R9 Series S hardware test established that
// this exact ICD can create/present with CoreWindow through patched Mesa.
static HMODULE r10_dozen_icd = nullptr;

static bool R10_WINRT_VulkanLoadLibrary(SDL_VideoDevice* device, const char* path)
{
    if (r10_dozen_icd || device->vulkan_config.loader_handle)
        return SDL_SetError("R10 Dozen already loaded");
    if (path && SDL_strcmp(path, "vulkan_dzn.dll") != 0)
        return SDL_SetError("R10 WinRT only loads packaged vulkan_dzn.dll");
    HMODULE dll = LoadPackagedLibrary(L"vulkan_dzn.dll", 0);
    if (!dll) return SDL_SetError("R10 LoadPackagedLibrary(vulkan_dzn.dll) failed");
    auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        GetProcAddress(dll, "vk_icdGetInstanceProcAddr"));
    if (!gipa) {
        FreeLibrary(dll);
        return SDL_SetError("R10 Dozen ICD entrypoint unavailable");
    }
    auto enumerator = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
        gipa(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties"));
    if (!enumerator) {
        FreeLibrary(dll);
        return SDL_SetError("R10 Dozen cannot enumerate extensions");
    }
    uint32_t count = 0;
    VkResult result = enumerator(nullptr, &count, nullptr);
    if (result != VK_SUCCESS || !count || count > 256) {
        FreeLibrary(dll);
        return SDL_SetError("R10 Dozen extension enumeration failed (%d)", int(result));
    }
    VkExtensionProperties ext[256] = {};
    result = enumerator(nullptr, &count, ext);
    bool surface = false, win32 = false;
    for (uint32_t i = 0; i < count && result == VK_SUCCESS; ++i) {
        surface |= SDL_strcmp(ext[i].extensionName, VK_KHR_SURFACE_EXTENSION_NAME) == 0;
        win32 |= SDL_strcmp(ext[i].extensionName, VK_KHR_WIN32_SURFACE_EXTENSION_NAME) == 0;
    }
    if (result != VK_SUCCESS || !surface || !win32) {
        FreeLibrary(dll);
        return SDL_SetError("R10 Dozen does not expose CoreWindow WSI");
    }
    r10_dozen_icd = dll;
    device->vulkan_config.loader_handle = reinterpret_cast<SDL_SharedObject*>(dll);
    device->vulkan_config.vkGetInstanceProcAddr = reinterpret_cast<SDL_FunctionPointer>(gipa);
    device->vulkan_config.vkEnumerateInstanceExtensionProperties =
        reinterpret_cast<SDL_FunctionPointer>(enumerator);
    SDL_strlcpy(device->vulkan_config.loader_path, "vulkan_dzn.dll",
                sizeof(device->vulkan_config.loader_path));
    return true;
}

static void R10_WINRT_VulkanUnloadLibrary(SDL_VideoDevice* device)
{
    if (r10_dozen_icd) FreeLibrary(r10_dozen_icd);
    r10_dozen_icd = nullptr;
    device->vulkan_config.loader_handle = nullptr;
    device->vulkan_config.vkGetInstanceProcAddr = nullptr;
    device->vulkan_config.vkEnumerateInstanceExtensionProperties = nullptr;
}

static const char* const* R10_WINRT_VulkanExtensions(SDL_VideoDevice*, Uint32* count)
{
    static const char* const exts[] = {
        VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME
    };
    if (count) *count = SDL_arraysize(exts);
    return exts;
}

static bool R10_WINRT_VulkanCreateSurface(
    SDL_VideoDevice* device, SDL_Window* window, VkInstance instance,
    const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface)
{
    if (!r10_dozen_icd || !window || !surface)
        return SDL_SetError("R10 Vulkan missing Dozen/window/surface");
    SDL_WindowData* win = window->internal;
    if (!win || !win->coreWindow.Get())
        return SDL_SetError("R10 CoreWindow unavailable");
    auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        device->vulkan_config.vkGetInstanceProcAddr);
    auto create = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
        gipa(instance, "vkCreateWin32SurfaceKHR"));
    if (!create) return SDL_SetError("R10 vkCreateWin32SurfaceKHR missing");
    VkWin32SurfaceCreateInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    info.hinstance = GetModuleHandleW(nullptr);
    info.hwnd = reinterpret_cast<HWND>(win->coreWindow.Get());
    const VkResult rc = create(instance, &info, allocator, surface);
    return rc == VK_SUCCESS ? true
        : SDL_SetError("R10 CoreWindow Vulkan surface creation failed (%d)", int(rc));
}

static void R10_WINRT_VulkanDestroySurface(
    SDL_VideoDevice* device, VkInstance instance, VkSurfaceKHR surface,
    const VkAllocationCallbacks* allocator)
{
    if (r10_dozen_icd)
        SDL_Vulkan_DestroySurface_Internal(
            reinterpret_cast<void*>(device->vulkan_config.vkGetInstanceProcAddr),
            instance, surface, allocator);
}

static bool R10_WINRT_VulkanPresentationSupport(
    SDL_VideoDevice* device, VkInstance instance, VkPhysicalDevice gpu,
    Uint32 family)
{
    if (!r10_dozen_icd) return SDL_SetError("R10 Dozen not loaded");
    auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        device->vulkan_config.vkGetInstanceProcAddr);
    auto support = reinterpret_cast<PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR>(
        gipa(instance, "vkGetPhysicalDeviceWin32PresentationSupportKHR"));
    if (!support) return SDL_SetError("R10 Vulkan presentation support function missing");
    return support(gpu, family) != VK_FALSE;
}
#endif // SDL_VIDEO_VULKAN

'''
CALLBACKS = '''\
#if defined(SDL_VIDEO_VULKAN)
    device->Vulkan_LoadLibrary = R10_WINRT_VulkanLoadLibrary;
    device->Vulkan_UnloadLibrary = R10_WINRT_VulkanUnloadLibrary;
    device->Vulkan_GetInstanceExtensions = R10_WINRT_VulkanExtensions;
    device->Vulkan_CreateSurface = R10_WINRT_VulkanCreateSurface;
    device->Vulkan_DestroySurface = R10_WINRT_VulkanDestroySurface;
    device->Vulkan_GetPresentationSupport = R10_WINRT_VulkanPresentationSupport;
#endif
'''


def patch(repo: Path) -> None:
    path = repo / "src/video/winrt/SDL_winrtvideo.cpp"
    if not path.is_file():
        raise ValueError(f"Missing pinned SDL3 WinRT driver: {path}")
    original = path.read_text(encoding="utf-8")
    if "R10_WINRT_VulkanLoadLibrary" in original:
        raise ValueError("SDL3 tree already patched; refusing double patch")
    for anchor in (HEADER_ANCHOR, CALLBACK_ANCHOR):
        if original.count(anchor) != 1:
            raise ValueError(f"Changed SDL3 fork source anchor: {anchor.strip()}")
    source = original.replace(HEADER_ANCHOR, HEADER_ANCHOR + HEADER, 1)
    source = source.replace(
        "// Initialization/Query functions\n",
        SHIM + "// Initialization/Query functions\n", 1)
    source = source.replace(CALLBACK_ANCHOR, CALLBACK_ANCHOR + CALLBACKS, 1)
    path.write_text(source, encoding="utf-8")
    print("R10: inserted packaged-Dozen SDL_Vulkan callbacks for real WinRT CoreWindow")
    print("Not a real game frame; use patched Mesa and full UWP game resources")


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--sdl-root", type=Path, required=True)
    args = p.parse_args()
    try:
        patch(args.sdl_root.resolve())
    except (OSError, ValueError) as exc:
        p.exit(1, "R10 SDL Vulkan bridge: " + str(exc) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
