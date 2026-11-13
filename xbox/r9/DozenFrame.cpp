// R9: real Vulkan/Dozen/D3D12 swapchain on Xbox UWP, NO Nintendo assets.
// The compatible Dozen ICD is built and packaged separately. This is not
// NSMBU's GX2 output. Keep R7D's proven package installed side-by-side.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#include <windows.h>
#include <vulkan/vulkan.h>
#include "DozenFrame.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>

namespace nsmbur9 {
namespace {
void line(Log log, const char* event, VkResult value) {
    char buffer[220] = {};
    sprintf_s(buffer, "[R9] %s: VkResult=%d", event, static_cast<int>(value));
    log(buffer);
}
void message(Log log, const char* value) { log(value); }
template<typename T>
T instance_fn(PFN_vkGetInstanceProcAddr gip, VkInstance instance,
              const char* name) {
    return reinterpret_cast<T>(gip(instance, name));
}
template<typename T>
T device_fn(PFN_vkGetDeviceProcAddr gdp, VkDevice device,
            const char* name) {
    return reinterpret_cast<T>(gdp(device, name));
}
bool has_extension(const std::vector<VkExtensionProperties>& extensions,
                   const char* name) {
    for (const auto& extension : extensions)
        if (strcmp(extension.extensionName, name) == 0) return true;
    return false;
}
uint32_t first_flag(uint32_t flags) {
    return flags & (~flags + 1u);
}
} // namespace

bool RunDozenFrame(IUnknown* coreWindow, Log log) noexcept {
    if (!log) return false;
    if (!coreWindow) { log("[R9] ERROR: missing live CoreWindow"); return false; }
    // Xbox has NO system Vulkan loader. Load only the private packaged
    // Mesa Dozen ICD; do not try desktop vulkan-1.dll / load from disk.
    HMODULE dozen = LoadPackagedLibrary(L"vulkan_dzn.dll", 0);
    if (!dozen) {
        char tmp[190] = {};
        sprintf_s(tmp, "[R9] STOP: packaged Dozen DLL unavailable, GetLastError=%lu",
                  GetLastError());
        log(tmp);
        return false;
    }
    message(log, "[R9] DOZEN DLL LOADED from UWP package");
    auto gip = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        GetProcAddress(dozen, "vk_icdGetInstanceProcAddr"));
    if (!gip) {
        log("[R9] STOP: Dozen ICD missing vk_icdGetInstanceProcAddr");
        FreeLibrary(dozen);
        return false;
    }
    // Use the ICD function-pointer ABI. Neither the application nor the
    // linker requires vulkan-1.dll, which isn't provided on Xbox.
    auto enumerateExtensions = instance_fn<PFN_vkEnumerateInstanceExtensionProperties>(
        gip, VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
    auto createInstance = instance_fn<PFN_vkCreateInstance>(
        gip, VK_NULL_HANDLE, "vkCreateInstance");
    if (!enumerateExtensions || !createInstance) {
        log("[R9] STOP: ICD instance entry functions missing");
        FreeLibrary(dozen);
        return false;
    }
    uint32_t count = 0;
    VkResult rc = enumerateExtensions(nullptr, &count, nullptr);
    if (rc != VK_SUCCESS || count > 256u) {
        line(log, "instance extensions unavailable", rc);
        FreeLibrary(dozen);
        return false;
    }
    std::vector<VkExtensionProperties> extensions(count);
    if (count) rc = enumerateExtensions(nullptr, &count, extensions.data());
    if (rc != VK_SUCCESS ||
        !has_extension(extensions, VK_KHR_SURFACE_EXTENSION_NAME) ||
        !has_extension(extensions, VK_KHR_WIN32_SURFACE_EXTENSION_NAME)) {
        line(log, "STOP: WSI instance extensions missing", rc);
        FreeLibrary(dozen);
        return false;
    }
    const char* ext[] = {
        VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME
    };
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "NSMBU R9 Dozen Vulkan GPU Integration";
    app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app.pEngineName = "NSMBU Xbox R9 experimental";
    app.apiVersion = VK_API_VERSION_1_2; // measured Smash4 baseline is 1.2.x
    VkInstanceCreateInfo info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = 2;
    info.ppEnabledExtensionNames = ext;
    VkInstance instance = VK_NULL_HANDLE;
    rc = createInstance(&info, nullptr, &instance);
    if (rc != VK_SUCCESS || !instance) {
        line(log, "STOP: vkCreateInstance", rc);
        FreeLibrary(dozen);
        return false;
    }
    message(log, "[R9] Vulkan instance CREATED (Mesa Dozen, API request 1.2)");

#define LOAD_INSTANCE(NAME) auto NAME = instance_fn<PFN_##NAME>(gip, instance, #NAME)
    LOAD_INSTANCE(vkDestroyInstance);
    LOAD_INSTANCE(vkEnumeratePhysicalDevices);
    LOAD_INSTANCE(vkGetPhysicalDeviceProperties);
    LOAD_INSTANCE(vkGetPhysicalDeviceFeatures);
    LOAD_INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties);
    LOAD_INSTANCE(vkEnumerateDeviceExtensionProperties);
    LOAD_INSTANCE(vkCreateDevice);
    LOAD_INSTANCE(vkCreateWin32SurfaceKHR);
    LOAD_INSTANCE(vkDestroySurfaceKHR);
    LOAD_INSTANCE(vkGetPhysicalDeviceSurfaceSupportKHR);
    LOAD_INSTANCE(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    LOAD_INSTANCE(vkGetPhysicalDeviceSurfaceFormatsKHR);
    auto getDeviceProc = instance_fn<PFN_vkGetDeviceProcAddr>(
        gip, instance, "vkGetDeviceProcAddr");
#undef LOAD_INSTANCE
    bool passed = false;
    VkDevice device = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkSemaphore available = VK_NULL_HANDLE, rendered = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    PFN_vkDestroyDevice destroyDevice = nullptr;
    PFN_vkDeviceWaitIdle deviceWaitIdle = nullptr;
    PFN_vkDestroySwapchainKHR destroySwapchain = nullptr;
    PFN_vkDestroyCommandPool destroyCommandPool = nullptr;
    PFN_vkDestroySemaphore destroySemaphore = nullptr;
    PFN_vkDestroyFence destroyFence = nullptr;
    // A single bounded attempt: no arbitrary game input, no background GPU work.
    do {
        if (!vkEnumeratePhysicalDevices || !vkCreateDevice ||
            !vkGetPhysicalDeviceProperties || !vkGetPhysicalDeviceFeatures ||
            !vkGetPhysicalDeviceQueueFamilyProperties ||
            !vkEnumerateDeviceExtensionProperties ||
            !vkCreateWin32SurfaceKHR || !vkGetPhysicalDeviceSurfaceSupportKHR ||
            !vkGetPhysicalDeviceSurfaceCapabilitiesKHR ||
            !vkGetPhysicalDeviceSurfaceFormatsKHR || !getDeviceProc) {
            log("[R9] STOP: missing required Vulkan instance functions"); break;
        }
        uint32_t nGpu = 0;
        rc = vkEnumeratePhysicalDevices(instance, &nGpu, nullptr);
        if (rc != VK_SUCCESS || nGpu == 0 || nGpu > 8) {
            line(log, "STOP: no Vulkan GPU", rc); break;
        }
        std::vector<VkPhysicalDevice> gpus(nGpu);
        rc = vkEnumeratePhysicalDevices(instance, &nGpu, gpus.data());
        if (rc != VK_SUCCESS) { line(log, "GPU enumeration", rc); break; }
        VkPhysicalDevice gpu = gpus.front();
        VkPhysicalDeviceProperties props = {};
        vkGetPhysicalDeviceProperties(gpu, &props);
        char description[270] = {};
        sprintf_s(description, "[R9] REAL GPU: %s Vulkan=%u.%u.%u storageRange=%u MiB",
                  props.deviceName, VK_VERSION_MAJOR(props.apiVersion),
                  VK_VERSION_MINOR(props.apiVersion),
                  VK_VERSION_PATCH(props.apiVersion),
                  props.limits.maxStorageBufferRange / (1024u * 1024u));
        log(description);
        VkPhysicalDeviceFeatures feats = {};
        vkGetPhysicalDeviceFeatures(gpu, &feats);
        sprintf_s(description, "[R9] GPU features: geometry=%u tessellation=%u shaderInt64=%u",
                  feats.geometryShader, feats.tessellationShader, feats.shaderInt64);
        log(description);
        uint32_t nQueue = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &nQueue, nullptr);
        if (nQueue == 0 || nQueue > 64) { log("[R9] STOP: no queue families"); break; }
        std::vector<VkQueueFamilyProperties> queues(nQueue);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &nQueue, queues.data());
        uint32_t queueFamily = UINT32_MAX;
        for (uint32_t i = 0; i < nQueue; i++) {
            if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                queueFamily = i; break;
            }
        }
        if (queueFamily == UINT32_MAX) { log("[R9] STOP: no graphics queue"); break; }
        uint32_t nDevExt = 0;
        rc = vkEnumerateDeviceExtensionProperties(gpu, nullptr, &nDevExt, nullptr);
        if (rc != VK_SUCCESS || nDevExt > 512) {
            line(log, "device extensions", rc); break;
        }
        std::vector<VkExtensionProperties> devExts(nDevExt);
        if (nDevExt) rc = vkEnumerateDeviceExtensionProperties(
            gpu, nullptr, &nDevExt, devExts.data());
        if (rc != VK_SUCCESS || !has_extension(devExts, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            line(log, "STOP: no VK_KHR_swapchain", rc); break;
        }
        // An actual Win32Surface uses the IUnknown* CoreWindow as HWND in
        // Smash4's *patched* Mesa WSI. Unpatched desktop Mesa is not enough.
        VkWin32SurfaceCreateInfoKHR surf = {
            VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR
        };
        surf.hinstance = GetModuleHandleW(nullptr);
        surf.hwnd = reinterpret_cast<HWND>(coreWindow);
        rc = vkCreateWin32SurfaceKHR(instance, &surf, nullptr, &surface);
        if (rc != VK_SUCCESS || !surface) {
            line(log, "STOP: vkCreateWin32SurfaceKHR on CoreWindow", rc); break;
        }
        VkBool32 support = VK_FALSE;
        rc = vkGetPhysicalDeviceSurfaceSupportKHR(
            gpu, queueFamily, surface, &support);
        if (rc != VK_SUCCESS || !support) {
            line(log, "STOP: no CoreWindow surface support", rc); break;
        }
        VkSurfaceCapabilitiesKHR capabilities = {};
        rc = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            gpu, surface, &capabilities);
        if (rc != VK_SUCCESS) { line(log, "surface capabilities", rc); break; }
        if ((capabilities.supportedUsageFlags &
             (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
            != (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
            log("[R9] STOP: swapchain cannot support clear/transfer destination");
            break;
        }
        uint32_t nFormats = 0;
        rc = vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &nFormats, nullptr);
        if (rc != VK_SUCCESS || !nFormats || nFormats > 64) {
            line(log, "STOP: missing WSI formats", rc); break;
        }
        std::vector<VkSurfaceFormatKHR> formats(nFormats);
        rc = vkGetPhysicalDeviceSurfaceFormatsKHR(
            gpu, surface, &nFormats, formats.data());
        if (rc != VK_SUCCESS) { line(log, "WSI formats", rc); break; }
        VkSurfaceFormatKHR format = formats.front();
        for (auto& x : formats) {
            if (x.format == VK_FORMAT_B8G8R8A8_UNORM) {
                format = x; break;
            }
        }
        float priority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo = {
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO
        };
        queueInfo.queueFamilyIndex = queueFamily;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        const char* swapchainExt = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo deviceInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        deviceInfo.enabledExtensionCount = 1;
        deviceInfo.ppEnabledExtensionNames = &swapchainExt;
        rc = vkCreateDevice(gpu, &deviceInfo, nullptr, &device);
        if (rc != VK_SUCCESS || !device) {
            line(log, "STOP: vkCreateDevice", rc); break;
        }
#define LOAD_DEVICE(NAME) auto NAME = device_fn<PFN_##NAME>(getDeviceProc, device, #NAME)
        LOAD_DEVICE(vkGetDeviceQueue);
        LOAD_DEVICE(vkCreateSwapchainKHR);
        LOAD_DEVICE(vkGetSwapchainImagesKHR);
        LOAD_DEVICE(vkAcquireNextImageKHR);
        LOAD_DEVICE(vkQueuePresentKHR);
        LOAD_DEVICE(vkCreateCommandPool);
        LOAD_DEVICE(vkAllocateCommandBuffers);
        LOAD_DEVICE(vkBeginCommandBuffer);
        LOAD_DEVICE(vkEndCommandBuffer);
        LOAD_DEVICE(vkCmdPipelineBarrier);
        LOAD_DEVICE(vkCmdClearColorImage);
        LOAD_DEVICE(vkCreateSemaphore);
        LOAD_DEVICE(vkCreateFence);
        LOAD_DEVICE(vkQueueSubmit);
        LOAD_DEVICE(vkWaitForFences);
        destroyDevice = device_fn<PFN_vkDestroyDevice>(getDeviceProc, device, "vkDestroyDevice");
        deviceWaitIdle = device_fn<PFN_vkDeviceWaitIdle>(getDeviceProc, device, "vkDeviceWaitIdle");
        destroySwapchain = device_fn<PFN_vkDestroySwapchainKHR>(
            getDeviceProc, device, "vkDestroySwapchainKHR");
        destroyCommandPool = device_fn<PFN_vkDestroyCommandPool>(
            getDeviceProc, device, "vkDestroyCommandPool");
        destroySemaphore = device_fn<PFN_vkDestroySemaphore>(
            getDeviceProc, device, "vkDestroySemaphore");
        destroyFence = device_fn<PFN_vkDestroyFence>(
            getDeviceProc, device, "vkDestroyFence");
#undef LOAD_DEVICE
        if (!vkGetDeviceQueue || !vkCreateSwapchainKHR || !vkGetSwapchainImagesKHR ||
            !vkAcquireNextImageKHR || !vkQueuePresentKHR || !vkCreateCommandPool ||
            !vkAllocateCommandBuffers || !vkBeginCommandBuffer || !vkEndCommandBuffer ||
            !vkCmdPipelineBarrier || !vkCmdClearColorImage || !vkCreateSemaphore ||
            !vkCreateFence || !vkQueueSubmit || !vkWaitForFences ||
            !destroyDevice || !deviceWaitIdle || !destroySwapchain ||
            !destroyCommandPool || !destroySemaphore || !destroyFence) {
            log("[R9] STOP: required Vulkan device functions unavailable"); break;
        }
        VkQueue queue = VK_NULL_HANDLE;
        vkGetDeviceQueue(device, queueFamily, 0, &queue);
        VkExtent2D extent = capabilities.currentExtent;
        if (extent.width == UINT32_MAX) {
            extent.width = std::max(capabilities.minImageExtent.width,
                std::min(1280u, capabilities.maxImageExtent.width));
            extent.height = std::max(capabilities.minImageExtent.height,
                std::min(720u, capabilities.maxImageExtent.height));
        }
        uint32_t imageCount = std::max(2u, capabilities.minImageCount);
        if (capabilities.maxImageCount && imageCount > capabilities.maxImageCount)
            imageCount = capabilities.maxImageCount;
        VkSwapchainCreateInfoKHR sci = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
        sci.surface = surface;
        sci.minImageCount = imageCount;
        sci.imageFormat = format.format;
        sci.imageColorSpace = format.colorSpace;
        sci.imageExtent = extent;
        sci.imageArrayLayers = 1;
        sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        sci.preTransform = capabilities.currentTransform;
        sci.compositeAlpha = (capabilities.supportedCompositeAlpha &
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ?
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR :
            static_cast<VkCompositeAlphaFlagBitsKHR>(
                first_flag(capabilities.supportedCompositeAlpha));
        sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        sci.clipped = VK_TRUE;
        rc = vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain);
        if (rc != VK_SUCCESS || !swapchain) {
            line(log, "STOP: CreateSwapchainForCoreWindow / Vulkan swapchain", rc);
            break;
        }
        message(log, "[R9] SWAPCHAIN CREATED on actual Xbox CoreWindow");
        uint32_t nImages = 0;
        rc = vkGetSwapchainImagesKHR(device, swapchain, &nImages, nullptr);
        if (rc != VK_SUCCESS || !nImages || nImages > 16) {
            line(log, "swapchain images", rc); break;
        }
        std::vector<VkImage> images(nImages);
        rc = vkGetSwapchainImagesKHR(device, swapchain, &nImages, images.data());
        if (rc != VK_SUCCESS) { line(log, "swapchain image handles", rc); break; }
        VkSemaphoreCreateInfo sem = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        rc = vkCreateSemaphore(device, &sem, nullptr, &available);
        if (rc != VK_SUCCESS) { line(log, "create acquire semaphore", rc); break; }
        rc = vkCreateSemaphore(device, &sem, nullptr, &rendered);
        if (rc != VK_SUCCESS) { line(log, "create render semaphore", rc); break; }
        VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        rc = vkCreateFence(device, &fci, nullptr, &fence);
        if (rc != VK_SUCCESS) { line(log, "create fence", rc); break; }
        uint32_t imageIndex = 0;
        rc = vkAcquireNextImageKHR(device, swapchain, 3000000000ull,
                                  available, VK_NULL_HANDLE, &imageIndex);
        if (rc != VK_SUCCESS && rc != VK_SUBOPTIMAL_KHR) {
            line(log, "STOP: swapchain acquire image", rc); break;
        }
        VkCommandPoolCreateInfo cpi = {
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO
        };
        cpi.queueFamilyIndex = queueFamily;
        rc = vkCreateCommandPool(device, &cpi, nullptr, &pool);
        if (rc != VK_SUCCESS) { line(log, "command pool", rc); break; }
        VkCommandBufferAllocateInfo alloc = {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
        };
        alloc.commandPool = pool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        rc = vkAllocateCommandBuffers(device, &alloc, &cmd);
        if (rc != VK_SUCCESS) { line(log, "allocate command buffer", rc); break; }
        VkCommandBufferBeginInfo begin = {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        rc = vkBeginCommandBuffer(cmd, &begin);
        if (rc != VK_SUCCESS) { line(log, "begin command buffer", rc); break; }
        VkImageMemoryBarrier barrier = {
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER
        };
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = images[imageIndex];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
            1, &barrier);
        VkClearColorValue darkCyan = {};
        darkCyan.float32[0] = 0.03f;
        darkCyan.float32[1] = 0.23f;
        darkCyan.float32[2] = 0.36f;
        darkCyan.float32[3] = 1.0f;
        VkImageSubresourceRange range = {};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.levelCount = 1;
        range.layerCount = 1;
        vkCmdClearColorImage(cmd, images[imageIndex],
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &darkCyan, 1, &range);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr,
            1, &barrier);
        rc = vkEndCommandBuffer(cmd);
        if (rc != VK_SUCCESS) { line(log, "end command buffer", rc); break; }
        VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &available;
        submit.pWaitDstStageMask = &waitStages;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &rendered;
        rc = vkQueueSubmit(queue, 1, &submit, fence);
        if (rc != VK_SUCCESS) { line(log, "GPU queue submit", rc); break; }
        VkPresentInfoKHR present = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &rendered;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &imageIndex;
        rc = vkQueuePresentKHR(queue, &present);
        if (rc != VK_SUCCESS && rc != VK_SUBOPTIMAL_KHR) {
            line(log, "GPU present", rc); break;
        }
        rc = vkWaitForFences(device, 1, &fence, VK_TRUE, 3000000000ull);
        if (rc != VK_SUCCESS) { line(log, "GPU fence wait", rc); break; }
        // No game draw calls were issued. A Vulkan clear is proof of actual
        // Dozen driver + CoreWindow WSI + D3D12 presentation, nothing more.
        message(log, "[R9] REAL VULKAN/DOZEN FRAME PRESENT PASS (synthetic color, NOT NSMBU GX2)");
        passed = true;
    } while (false);

    if (device && deviceWaitIdle) deviceWaitIdle(device);
    if (fence && destroyFence) destroyFence(device, fence, nullptr);
    if (rendered && destroySemaphore) destroySemaphore(device, rendered, nullptr);
    if (available && destroySemaphore) destroySemaphore(device, available, nullptr);
    if (pool && destroyCommandPool) destroyCommandPool(device, pool, nullptr);
    if (swapchain && destroySwapchain) destroySwapchain(device, swapchain, nullptr);
    if (device && destroyDevice) destroyDevice(device, nullptr);
    if (surface && vkDestroySurfaceKHR) vkDestroySurfaceKHR(instance, surface, nullptr);
    if (vkDestroyInstance) vkDestroyInstance(instance, nullptr);
    FreeLibrary(dozen);
    return passed;
}
} // namespace nsmbur9
