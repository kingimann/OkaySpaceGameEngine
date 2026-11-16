// Vulkan loader seam: see okay/Render/VulkanProbe.hpp. Written from the Vulkan
// specification (instance creation, physical device enumeration, the layout of
// VkPhysicalDeviceProperties / VkPhysicalDeviceLimits, queue family properties).
// No Vulkan header or library is used; only the few types below are declared.
#include "okay/Render/VulkanProbe.hpp"
#include <cstdlib>
#include <cstring>
#include <cstdio>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace okay {
namespace {

// ---- the few Vulkan declarations we need (from the specification) ----------
#if defined(_WIN32)
#  define OKAY_VKAPI __stdcall
#else
#  define OKAY_VKAPI
#endif
using VkResult = std::int32_t;               // VK_SUCCESS = 0
constexpr VkResult kVkSuccess = 0;
constexpr std::uint32_t kVkStructureTypeApplicationInfo = 0;
constexpr std::uint32_t kVkStructureTypeInstanceCreateInfo = 1;
constexpr std::uint32_t kVkApiVersion1_0 = (0u << 29) | (1u << 22);   // VK_MAKE_API_VERSION(0,1,0,0)

struct VkApplicationInfo {
    std::uint32_t sType;
    const void* pNext;
    const char* pApplicationName;
    std::uint32_t applicationVersion;
    const char* pEngineName;
    std::uint32_t engineVersion;
    std::uint32_t apiVersion;
};
struct VkInstanceCreateInfo {
    std::uint32_t sType;
    const void* pNext;
    std::uint32_t flags;
    const VkApplicationInfo* pApplicationInfo;
    std::uint32_t enabledLayerCount;
    const char* const* ppEnabledLayerNames;
    std::uint32_t enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
};
struct VkQueueFamilyPropertiesRaw {
    std::uint32_t queueFlags;
    std::uint32_t queueCount;
    std::uint32_t timestampValidBits;
    std::uint32_t minImageTransferGranularity[3];   // VkExtent3D
};
using VkInstance = void*;
using VkPhysicalDevice = void*;
using PFN_vkVoidFunction = void (OKAY_VKAPI*)();
using PFN_vkGetInstanceProcAddr = PFN_vkVoidFunction (OKAY_VKAPI*)(VkInstance, const char*);
using PFN_vkCreateInstance = VkResult (OKAY_VKAPI*)(const VkInstanceCreateInfo*, const void*, VkInstance*);
using PFN_vkDestroyInstance = void (OKAY_VKAPI*)(VkInstance, const void*);
using PFN_vkEnumerateInstanceVersion = VkResult (OKAY_VKAPI*)(std::uint32_t*);
using PFN_vkEnumeratePhysicalDevices = VkResult (OKAY_VKAPI*)(VkInstance, std::uint32_t*, VkPhysicalDevice*);
using PFN_vkGetPhysicalDeviceProperties = void (OKAY_VKAPI*)(VkPhysicalDevice, void*);
using PFN_vkGetPhysicalDeviceQueueFamilyProperties = void (OKAY_VKAPI*)(VkPhysicalDevice, std::uint32_t*, VkQueueFamilyPropertiesRaw*);

// Offsets into VkPhysicalDeviceProperties (spec layout, 64-bit and 32-bit alike
// because every member before `limits` is 4 or 1 bytes and `limits` is 8-aligned).
constexpr std::size_t kOffApiVersion = 0, kOffDriverVersion = 4, kOffVendorId = 8, kOffDeviceId = 12,
                      kOffDeviceType = 16, kOffDeviceName = 20, kDeviceNameSize = 256,
                      kOffLimits = 296;
// Offsets inside VkPhysicalDeviceLimits (504 bytes).
constexpr std::size_t kLimMaxImageDimension2D = 4, kLimMaxPushConstantsSize = 32,
                      kLimMaxBoundDescriptorSets = 64, kLimMaxComputeWorkGroupInvocations = 232,
                      kLimFramebufferColorSampleCounts = 376, kLimFramebufferDepthSampleCounts = 380;

std::uint32_t ReadU32(const unsigned char* base, std::size_t off) {
    std::uint32_t v; std::memcpy(&v, base + off, 4); return v;
}

// ---- dynamic loading --------------------------------------------------------
void* OpenLibrary(const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(LoadLibraryA(name));
#else
    return dlopen(name, RTLD_NOW | RTLD_LOCAL);
#endif
}
void* FindSymbol(void* lib, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(lib), name));
#else
    return dlsym(lib, name);
#endif
}
void CloseLibrary(void* lib) {
    if (!lib) return;
#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(lib));
#else
    dlclose(lib);
#endif
}

}  // namespace

bool VulkanProbeEnabledFrom(const char* v) {
    if (!v || !*v) return false;
    const std::string s = v;
    return s == "1" || s == "true" || s == "TRUE" || s == "on" || s == "ON" || s == "yes";
}
bool VulkanProbeEnabled() { return VulkanProbeEnabledFrom(std::getenv("OKAY_VULKAN_PROBE")); }

std::string VulkanVersionString(std::uint32_t packed) {
    char b[48];
    std::snprintf(b, sizeof(b), "%u.%u.%u", (packed >> 22) & 0x7Fu, (packed >> 12) & 0x3FFu, packed & 0xFFFu);
    return b;
}

const char* VulkanDeviceTypeName(std::uint32_t t) {
    switch (t) {
        case 1: return "integrated GPU";
        case 2: return "discrete GPU";
        case 3: return "virtual GPU";
        case 4: return "CPU (software)";
        default: return "other";
    }
}

std::string VulkanQueueFlagsString(std::uint32_t f) {
    std::string s;
    auto add = [&](std::uint32_t bit, const char* n) { if (f & bit) { if (!s.empty()) s += '+'; s += n; } };
    add(1, "graphics"); add(2, "compute"); add(4, "transfer"); add(8, "sparse");
    return s.empty() ? "none" : s;
}

bool ParseVulkanDeviceProperties(const void* buffer, std::size_t size, VulkanDeviceInfo& out) {
    if (!buffer || size < kVulkanPhysicalDevicePropertiesSize) return false;
    const unsigned char* b = static_cast<const unsigned char*>(buffer);
    out.apiVersion = ReadU32(b, kOffApiVersion);
    out.driverVersion = ReadU32(b, kOffDriverVersion);
    out.vendorId = ReadU32(b, kOffVendorId);
    out.deviceId = ReadU32(b, kOffDeviceId);
    out.deviceType = ReadU32(b, kOffDeviceType);
    const char* nm = reinterpret_cast<const char*>(b + kOffDeviceName);
    std::size_t n = 0; while (n < kDeviceNameSize && nm[n]) ++n;   // never trust the terminator
    out.name.assign(nm, n);
    const unsigned char* lim = b + kOffLimits;
    out.maxImageDimension2D = ReadU32(lim, kLimMaxImageDimension2D);
    out.maxPushConstantsSize = ReadU32(lim, kLimMaxPushConstantsSize);
    out.maxBoundDescriptorSets = ReadU32(lim, kLimMaxBoundDescriptorSets);
    out.maxComputeWorkGroupInvocations = ReadU32(lim, kLimMaxComputeWorkGroupInvocations);
    out.framebufferColorSampleCounts = ReadU32(lim, kLimFramebufferColorSampleCounts);
    out.framebufferDepthSampleCounts = ReadU32(lim, kLimFramebufferDepthSampleCounts);
    return true;
}

VulkanProbeResult RunVulkanProbe() {
    VulkanProbeResult r;
#if defined(_WIN32)
    static const char* const kNames[] = {"vulkan-1.dll"};
#elif defined(__APPLE__)
    static const char* const kNames[] = {"libvulkan.1.dylib", "libMoltenVK.dylib"};
#else
    static const char* const kNames[] = {"libvulkan.so.1", "libvulkan.so"};
#endif
    void* lib = nullptr;
    for (const char* n : kNames) {
        lib = OpenLibrary(n);
        if (lib) { r.loaderName = n; break; }
    }
    if (!lib) { r.error = "no Vulkan loader"; return r; }
    r.loaderFound = true;

    auto getInstProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(FindSymbol(lib, "vkGetInstanceProcAddr"));
    if (!getInstProc) { r.error = "the Vulkan loader has no vkGetInstanceProcAddr"; CloseLibrary(lib); return r; }

    // Global-level functions: instance handle is null.
    auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(getInstProc(nullptr, "vkCreateInstance"));
    auto enumVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(getInstProc(nullptr, "vkEnumerateInstanceVersion"));
    if (!createInstance) { r.error = "vkCreateInstance not found"; CloseLibrary(lib); return r; }
    if (enumVersion) { std::uint32_t v = 0; if (enumVersion(&v) == kVkSuccess) r.instanceVersion = v; }

    VkApplicationInfo app = {};
    app.sType = kVkStructureTypeApplicationInfo;
    app.pApplicationName = "OkayEngine GPU probe";
    app.pEngineName = "OkayEngine";
    app.apiVersion = kVkApiVersion1_0;
    VkInstanceCreateInfo ci = {};
    ci.sType = kVkStructureTypeInstanceCreateInfo;
    ci.pApplicationInfo = &app;
    VkInstance inst = nullptr;
    const VkResult cr = createInstance(&ci, nullptr, &inst);
    if (cr != kVkSuccess || !inst) {
        r.error = "vkCreateInstance failed (VkResult " + std::to_string(cr) + ")";
        CloseLibrary(lib);
        return r;
    }
    r.instanceCreated = true;

    auto destroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(getInstProc(inst, "vkDestroyInstance"));
    auto enumDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(getInstProc(inst, "vkEnumeratePhysicalDevices"));
    auto getProps = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(getInstProc(inst, "vkGetPhysicalDeviceProperties"));
    auto getQueues = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(getInstProc(inst, "vkGetPhysicalDeviceQueueFamilyProperties"));
    if (!enumDevices || !getProps || !getQueues) {
        r.error = "the loader is missing device query functions";
    } else {
        std::uint32_t n = 0;
        if (enumDevices(inst, &n, nullptr) != kVkSuccess) r.error = "vkEnumeratePhysicalDevices failed";
        else if (n > 0) {
            if (n > 16) n = 16;   // a probe, not a device manager
            std::vector<VkPhysicalDevice> devs(n, nullptr);
            const VkResult er = enumDevices(inst, &n, devs.data());   // VK_INCOMPLETE (5) is fine: we asked for fewer
            if (er != kVkSuccess && er != 5) r.error = "vkEnumeratePhysicalDevices failed";
            else for (std::uint32_t i = 0; i < n; ++i) {
                // 1024 bytes: more than the 824 the specification defines, zeroed, 8-aligned.
                alignas(8) unsigned char props[1024] = {};
                getProps(devs[i], props);
                VulkanDeviceInfo d;
                if (!ParseVulkanDeviceProperties(props, sizeof(props), d)) continue;
                std::uint32_t qn = 0;
                getQueues(devs[i], &qn, nullptr);
                if (qn > 32) qn = 32;
                std::vector<VkQueueFamilyPropertiesRaw> q(qn);
                if (qn) getQueues(devs[i], &qn, q.data());
                for (std::uint32_t k = 0; k < qn; ++k)
                    d.queueFamilies.push_back({q[k].queueFlags, q[k].queueCount, q[k].timestampValidBits});
                r.devices.push_back(std::move(d));
            }
        }
    }
    if (destroyInstance) destroyInstance(inst, nullptr);
    CloseLibrary(lib);
    return r;
}

std::string FormatVulkanProbe(const VulkanProbeResult& r) {
    std::string s;
    if (!r.loaderFound) return "Vulkan probe: no Vulkan loader found on this machine.\n";
    s += "Vulkan probe: loader " + r.loaderName + ", loader version " +
         (r.instanceVersion ? VulkanVersionString(r.instanceVersion) : std::string("1.0 (no vkEnumerateInstanceVersion)")) + "\n";
    if (!r.instanceCreated) { s += "  instance: FAILED - " + r.error + "\n"; return s; }
    s += "  instance: created and destroyed\n";
    if (!r.error.empty()) s += "  note: " + r.error + "\n";
    if (r.devices.empty()) s += "  devices: none\n";
    char b[320];
    for (size_t i = 0; i < r.devices.size(); ++i) {
        const VulkanDeviceInfo& d = r.devices[i];
        std::snprintf(b, sizeof(b), "  device %zu: %s (%s), Vulkan %s, vendor 0x%04X, device 0x%04X\n", i, d.name.c_str(),
                      VulkanDeviceTypeName(d.deviceType), VulkanVersionString(d.apiVersion).c_str(), d.vendorId, d.deviceId);
        s += b;
        std::snprintf(b, sizeof(b), "    limits: max image %u, descriptor sets %u, push constants %u bytes, compute invocations %u, MSAA colour 0x%X depth 0x%X\n",
                      d.maxImageDimension2D, d.maxBoundDescriptorSets, d.maxPushConstantsSize, d.maxComputeWorkGroupInvocations,
                      d.framebufferColorSampleCounts, d.framebufferDepthSampleCounts);
        s += b;
        for (size_t k = 0; k < d.queueFamilies.size(); ++k) {
            const VulkanQueueFamily& q = d.queueFamilies[k];
            std::snprintf(b, sizeof(b), "    queue family %zu: %u queue(s), %s\n", k, q.count, VulkanQueueFlagsString(q.flags).c_str());
            s += b;
        }
    }
    s += "  (probe only: the engine does not render with Vulkan yet)\n";
    return s;
}

}  // namespace okay
