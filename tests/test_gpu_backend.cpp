// Headless tests for the GPU backend contract (okay/Render/GpuBackend.hpp), the
// backend chooser and the Vulkan probe's pure helpers. No GPU, no GL, no Vulkan
// loader is needed. Written from the specification of the behaviour in
// docs/render_backends.md and the Vulkan specification's struct layouts.
#include "test_framework.hpp"
#include "okay/Render/GpuBackend.hpp"
#include "okay/Render/VulkanProbe.hpp"
#include <cstring>
#include <vector>

using namespace okay;

namespace {
// What the old player if-chain picked (first backend that returns a frame), as a list.
std::vector<RenderBackend> OldPlayerOrder(bool gpu, bool d3d12Opt, bool r12, bool r11, bool rgl, int fails) {
    std::vector<RenderBackend> v;
    if (gpu && (r12 || r11 || rgl) && fails < 3) {
        if (r12) v.push_back(RenderBackend::Direct3D12);   // d3d12Ready is only ever true when opted in
        if (r11) v.push_back(RenderBackend::Direct3D11);
        if (rgl) v.push_back(RenderBackend::OpenGL);
    }
    (void)d3d12Opt;
    v.push_back(RenderBackend::Software);
    return v;
}
std::vector<RenderBackend> ToVec(const GpuBackendOrder& o) {
    return std::vector<RenderBackend>(o.list, o.list + o.count);
}

struct FakeRenderer {
    int calls = 0, destroyed = 0;
    std::uint32_t px[4] = {1, 2, 3, 4};
    bool fail = false;
    int lastW = 0, lastH = 0, lastSamples = 0;
    float lastClearA = -1;
    const std::uint32_t* RenderToPixels(const Scene&, const Mat4&, const Vec3&, int w, int h, int samples,
                                        float, float, float, float a, const GameObject*) {
        ++calls; lastW = w; lastH = h; lastSamples = samples; lastClearA = a;
        return fail ? nullptr : px;
    }
    void Destroy() { ++destroyed; }
};
}  // namespace

int main() {
    RUN_SUITE("gpu_backend");

    // --- chooser equals the old player chain for every combination ---
    for (int m = 0; m < 128; ++m) {
        const bool gpu = m & 1, d3d12Opt = m & 2, a12 = m & 4, a11 = m & 8, agl = m & 16;
        const int fails = (m & 32) ? 3 : 0;
        const bool tracked = (m & 64);
        (void)tracked;
        // The player only starts Direct3D 12 when opted in, so "available" implies the opt-in.
        const bool r12 = a12 && d3d12Opt;
        GpuChooserInput in;
        in.gpuEnabled = gpu; in.allowD3D12 = d3d12Opt; in.tryAllowed = fails < 3;
        in.available[3] = r12; in.available[2] = a11; in.available[1] = agl;
        CHECK(ToVec(ChooseGpuBackends(in)) == OldPlayerOrder(gpu, d3d12Opt, r12, a11, agl, fails));
    }
    // --- the editor order: D3D11 then OpenGL, never D3D12 even if it were available ---
    {
        GpuChooserInput in;
        in.available[3] = true; in.available[2] = true; in.available[1] = true;   // allowD3D12 stays false
        auto v = ToVec(ChooseGpuBackends(in));
        CHECK(v.size() == 3 && v[0] == RenderBackend::Direct3D11 && v[1] == RenderBackend::OpenGL && v[2] == RenderBackend::Software);
        in.available[2] = false;
        v = ToVec(ChooseGpuBackends(in));
        CHECK(v.size() == 2 && v[0] == RenderBackend::OpenGL);
    }
    // --- software is always last and always present ---
    {
        GpuChooserInput in; in.gpuEnabled = false; in.available[1] = in.available[2] = in.available[3] = true;
        GpuBackendOrder o = ChooseGpuBackends(in);
        CHECK(o.count == 1 && o.list[0] == RenderBackend::Software && o.GpuCount() == 0);
    }
    // --- demoted backend and requirements ---
    {
        GpuChooserInput in; in.allowD3D12 = true;
        in.available[1] = in.available[2] = in.available[3] = true;
        in.disabled[3] = true;
        CHECK(ToVec(ChooseGpuBackends(in))[0] == RenderBackend::Direct3D11);
        in.disabled[3] = false;
        in.need.localShadows = true;    // Direct3D 12 has no local shadows
        CHECK(ToVec(ChooseGpuBackends(in))[0] == RenderBackend::Direct3D11);
        in.need.gpuSkinning = true; in.need.compute = true;   // nothing computes yet
        CHECK(ChooseGpuBackends(in).GpuCount() == 0);
    }

    // --- capability table ---
    {
        CHECK(GpuBackendCapabilities(RenderBackend::OpenGL).maxLights == 16);
        CHECK(GpuBackendCapabilities(RenderBackend::Direct3D11).directPresent);
        CHECK(!GpuBackendCapabilities(RenderBackend::OpenGL).directPresent);
        CHECK(!GpuBackendCapabilities(RenderBackend::Direct3D12).postChain);
        CHECK(!GpuBackendCapabilities(RenderBackend::Direct3D12).gpuSkinning);
        CHECK(GpuBackendCapabilities(RenderBackend::Software).maxMsaaSamples == 1);
        for (int b = 0; b < kRenderBackendCount; ++b) CHECK(!GpuBackendCapabilities((RenderBackend)b).compute);
        GpuRequirements none;
        for (int b = 0; b < kRenderBackendCount; ++b) CHECK(GpuMeetsRequirements(GpuBackendCapabilities((RenderBackend)b), none));
        GpuRequirements msaa; msaa.minMsaaSamples = 8;
        CHECK(!GpuMeetsRequirements(GpuBackendCapabilities(RenderBackend::OpenGL), msaa));
        // MSAA clamp from a Vulkan-style mask (flag value = count): 1, 2, 4, 8 supported = 0xF
        CHECK(GpuClampMsaaSamples(4, 0xF) == 4);
        CHECK(GpuClampMsaaSamples(16, 0xF) == 8);
        CHECK(GpuClampMsaaSamples(8, 0x5) == 4);   // 1 and 4 only
        CHECK(GpuClampMsaaSamples(4, 0) == 1);
        CHECK(GpuClampMsaaSamples(1, 0xF) == 1);
        // ids round trip
        for (int b = 0; b < kRenderBackendCount; ++b) {
            RenderBackend back = RenderBackend::Software;
            CHECK(ParseGpuBackendId(GpuBackendId((RenderBackend)b), back) && back == (RenderBackend)b);
        }
        RenderBackend junk; CHECK(!ParseGpuBackendId("vulkan", junk));
        CHECK(std::strcmp(GpuBackendLabel(RenderBackend::Direct3D12), "Direct3D 12 (GPU)") == 0);
        CHECK(GpuCapabilitiesText(RenderBackend::OpenGL).find("lights 16") != std::string::npos);
    }

    // --- failure tracker: player (permanent) and editor (5 s rest, then retry) ---
    {
        GpuFailureTracker t;
        CHECK(t.MayTry(0));
        CHECK(!t.Record(false, 0)); CHECK(!t.Record(false, 0));
        CHECK(t.MayTry(0));
        CHECK(t.Record(false, 0));              // third failure trips it
        CHECK(!t.MayTry(0) && !t.MayTry(1000000) && t.off);
        GpuFailureTracker ok;
        ok.Record(false, 0); ok.Record(false, 0); ok.Record(true, 0); ok.Record(false, 0); ok.Record(false, 0);
        CHECK(ok.MayTry(0));                    // a good frame reset the count
        GpuFailureTracker e;
        e.Record(false, 100, 3, 5000); e.Record(false, 100, 3, 5000);
        CHECK(e.Record(false, 100, 3, 5000));
        CHECK(!e.MayTry(100) && !e.MayTry(5099) && e.MayTry(5100) && e.MayTry(9999));
        CHECK(!e.off && e.fails == 0);
        // tick wrap-around: rest starts just before 2^32
        GpuFailureTracker w;
        const std::uint32_t near = 0xFFFFFF00u;
        w.Record(false, near, 1, 1000);
        CHECK(!w.MayTry(near + 10) && w.MayTry(near + 1000));   // wraps to a small number and still works
    }

    // --- adapter: same call as the renderer, hooks, failure, shutdown ---
    {
        FakeRenderer fr;
        int acq = 0, rel = 0;
        GpuRendererAdapter<FakeRenderer>::Hooks h;
        h.acquire = [&] { ++acq; return true; };
        h.release = [&] { ++rel; };
        GpuRendererAdapter<FakeRenderer> ad(RenderBackend::OpenGL, &fr, h);
        CHECK(ad.Id() == RenderBackend::OpenGL && !ad.Ready());
        CHECK(std::strcmp(ad.Name(), "OpenGL (GPU)") == 0);
        CHECK(ad.Capabilities().maxLights == 16);
        GpuFrameDesc f;
        CHECK(ad.RenderToPixels(f) == nullptr);       // not initialised, no scene
        CHECK(ad.Init() && ad.Ready());
        CHECK(ad.RenderToPixels(f) == nullptr && fr.calls == 0);   // still no scene
        Scene* fakeScene = reinterpret_cast<Scene*>(&fr);          // never dereferenced by FakeRenderer
        f.scene = fakeScene; f.width = 64; f.height = 32; f.samples = 4;
        const std::uint32_t* px = ad.RenderToPixels(f);
        CHECK(px == fr.px && fr.calls == 1 && fr.lastW == 64 && fr.lastH == 32 && fr.lastSamples == 4);
        CHECK(acq == 1 && rel == 1);
        fr.fail = true;
        CHECK(ad.RenderToPixels(f) == nullptr && acq == 2 && rel == 2);   // release still runs after a failed frame
        h.acquire = [&] { ++acq; return false; };
        GpuRendererAdapter<FakeRenderer> ad2(RenderBackend::OpenGL, &fr, h);
        ad2.Init(); fr.fail = false; const int before = fr.calls;
        CHECK(ad2.RenderToPixels(f) == nullptr && fr.calls == before);   // MakeCurrent failed: renderer not called
        ad.Shutdown();
        CHECK(fr.destroyed == 1 && !ad.Ready());
        GpuRendererAdapter<FakeRenderer> ad3(RenderBackend::Direct3D11, nullptr);
        CHECK(!ad3.Init() && !ad3.Ready());
        // Interface use
        IGpuBackend* base = &ad2; CHECK(base->Id() == RenderBackend::OpenGL);
    }

    // --- Vulkan probe pure helpers ---
    {
        CHECK(VulkanVersionString((1u << 22) | (3u << 12) | 250u) == "1.3.250");
        CHECK(VulkanVersionString(0) == "0.0.0");
        CHECK(std::string(VulkanDeviceTypeName(2)) == "discrete GPU");
        CHECK(std::string(VulkanDeviceTypeName(99)) == "other");
        CHECK(VulkanQueueFlagsString(7) == "graphics+compute+transfer");
        CHECK(VulkanQueueFlagsString(0) == "none");
        CHECK(!VulkanProbeEnabledFrom(nullptr) && !VulkanProbeEnabledFrom("") && !VulkanProbeEnabledFrom("0"));
        CHECK(VulkanProbeEnabledFrom("1") && VulkanProbeEnabledFrom("true"));

        // A properties buffer laid out per the specification: header fields, 256-byte name,
        // 16-byte UUID, limits at 296 (504 bytes), sparse properties (20 bytes) = 824.
        std::vector<unsigned char> buf(kVulkanPhysicalDevicePropertiesSize, 0);
        auto put = [&](std::size_t off, std::uint32_t v) { std::memcpy(&buf[off], &v, 4); };
        put(0, (1u << 22) | (2u << 12) | 7u); put(4, 0xABCD); put(8, 0x10DE); put(12, 0x2204); put(16, 2);
        std::memcpy(&buf[20], "Test GPU 9000", 14);
        put(296 + 4, 16384); put(296 + 32, 128); put(296 + 64, 8); put(296 + 232, 1024);
        put(296 + 376, 0x1F); put(296 + 380, 0xF);
        VulkanDeviceInfo d;
        CHECK(ParseVulkanDeviceProperties(buf.data(), buf.size(), d));
        CHECK(d.name == "Test GPU 9000" && d.vendorId == 0x10DE && d.deviceId == 0x2204 && d.deviceType == 2);
        CHECK(d.apiVersion == ((1u << 22) | (2u << 12) | 7u) && d.driverVersion == 0xABCD);
        CHECK(d.maxImageDimension2D == 16384 && d.maxPushConstantsSize == 128 && d.maxBoundDescriptorSets == 8);
        CHECK(d.maxComputeWorkGroupInvocations == 1024 && d.framebufferColorSampleCounts == 0x1F && d.framebufferDepthSampleCounts == 0xF);
        CHECK(GpuClampMsaaSamples(4, d.framebufferColorSampleCounts & d.framebufferDepthSampleCounts) == 4);
        CHECK(!ParseVulkanDeviceProperties(buf.data(), buf.size() - 1, d));
        CHECK(!ParseVulkanDeviceProperties(nullptr, 1000, d));
        // an unterminated name must not read past 256 bytes
        std::vector<unsigned char> full(kVulkanPhysicalDevicePropertiesSize, 'A');
        VulkanDeviceInfo d2; CHECK(ParseVulkanDeviceProperties(full.data(), full.size(), d2) && d2.name.size() == 256);

        // Report text
        VulkanProbeResult none;
        CHECK(FormatVulkanProbe(none).find("no Vulkan loader") != std::string::npos);
        VulkanProbeResult r; r.loaderFound = true; r.loaderName = "libvulkan.so.1"; r.instanceCreated = true;
        r.instanceVersion = (1u << 22) | (3u << 12); d.queueFamilies.push_back({7, 16, 64}); r.devices.push_back(d);
        const std::string rep = FormatVulkanProbe(r);
        CHECK(rep.find("Test GPU 9000 (discrete GPU)") != std::string::npos);
        CHECK(rep.find("queue family 0: 16 queue(s), graphics+compute+transfer") != std::string::npos);
        CHECK(rep.find("loader version 1.3.0") != std::string::npos);
        CHECK(rep.find("does not render with Vulkan") != std::string::npos);
        // The real loader: whatever this machine has, it must not crash and must be self-consistent.
        VulkanProbeResult live = RunVulkanProbe();
        CHECK(live.loaderFound || live.error == "no Vulkan loader");
        CHECK(!live.instanceCreated || live.loaderFound);
        std::cout << FormatVulkanProbe(live);
    }

    TEST_MAIN_RESULT();
}
