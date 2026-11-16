#pragma once
// ---------------------------------------------------------------------------
// Vulkan loader SEAM (OFF by default). Our own minimal dynamic loader: it opens
// the system Vulkan loader (vulkan-1.dll on Windows, libvulkan.so.1 elsewhere),
// resolves vkGetInstanceProcAddr itself, creates an instance, lists the physical
// devices (name, type, API version, queue families, a few limits) and shuts down.
// It does NOT render, create a surface or a logical device, and nothing in the
// engine calls it unless a person asks (OKAY_VULKAN_PROBE=1, see below).
//
// Written from the Vulkan specification (the 1.0 core chapters on instances,
// physical devices, VkPhysicalDeviceProperties / VkPhysicalDeviceLimits and
// queue families). The handful of types and constants we need are declared in
// VulkanProbe.cpp; no Vulkan header, SDK or library is used or linked.
//
// Pure helpers (version text, device-type names, parsing the properties buffer,
// the report text) are separate from the loading so tests can run them with no
// GPU and no loader.
// ---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace okay {

struct VulkanQueueFamily {
    std::uint32_t flags = 0;       // 1 graphics, 2 compute, 4 transfer, 8 sparse binding
    std::uint32_t count = 0;
    std::uint32_t timestampBits = 0;
};

struct VulkanDeviceInfo {
    std::string name;
    std::uint32_t apiVersion = 0;      // packed: variant 31..29, major 28..22, minor 21..12, patch 11..0
    std::uint32_t driverVersion = 0;   // vendor specific
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    std::uint32_t deviceType = 0;      // 0 other, 1 integrated, 2 discrete, 3 virtual, 4 CPU
    std::uint32_t maxImageDimension2D = 0;
    std::uint32_t maxBoundDescriptorSets = 0;
    std::uint32_t maxPushConstantsSize = 0;
    std::uint32_t maxComputeWorkGroupInvocations = 0;
    std::uint32_t framebufferColorSampleCounts = 0;   // bit mask, flag value = sample count
    std::uint32_t framebufferDepthSampleCounts = 0;
    std::vector<VulkanQueueFamily> queueFamilies;
};

struct VulkanProbeResult {
    bool loaderFound = false;
    std::string loaderName;            // which library file opened
    bool instanceCreated = false;
    std::uint32_t instanceVersion = 0; // 0 = Vulkan 1.0 loader (no vkEnumerateInstanceVersion)
    std::vector<VulkanDeviceInfo> devices;
    std::string error;                 // empty when everything worked
};

/// The switch: OKAY_VULKAN_PROBE=1 (or "true"/"on"). Default off.
bool VulkanProbeEnabled();
bool VulkanProbeEnabledFrom(const char* envValue);   // pure, for tests

/// Load the loader, make an instance, list devices, destroy the instance.
/// Never throws; every failure lands in `error`. Safe on a machine with no
/// Vulkan (loaderFound = false).
VulkanProbeResult RunVulkanProbe();

/// The plain-text report the self-test prints. "no Vulkan loader" when none.
std::string FormatVulkanProbe(const VulkanProbeResult& r);

// ---- pure helpers ----------------------------------------------------------
std::string VulkanVersionString(std::uint32_t packed);          // "1.3.250"
const char* VulkanDeviceTypeName(std::uint32_t deviceType);     // "discrete GPU"
std::string VulkanQueueFlagsString(std::uint32_t flags);        // "graphics+compute+transfer"
/// Size in bytes of VkPhysicalDeviceProperties as the specification lays it out.
inline constexpr std::size_t kVulkanPhysicalDevicePropertiesSize = 824;
/// Read the fields we use out of a raw VkPhysicalDeviceProperties buffer
/// (`size` must be >= kVulkanPhysicalDevicePropertiesSize). false when too small.
bool ParseVulkanDeviceProperties(const void* buffer, std::size_t size, VulkanDeviceInfo& out);

}  // namespace okay
