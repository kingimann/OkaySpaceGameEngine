// The profiler frame records job-system time when a job system is already running.
#include "test_framework.hpp"
#include "okay/Core/Profiler.hpp"
#include "okay/Core/Jobs.hpp"
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>

using namespace okay;

static void Spin(double ms) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds((long long)(ms * 1000.0));
    while (std::chrono::steady_clock::now() < until) {}
}

static double JobsMs(const Profiler::Frame& f) {
    for (const auto& b : f.buckets) if (b.name == "Jobs") return b.inclusive;
    return 0.0;
}

int main() {
    RUN_SUITE("profiler_jobs");
    JobSystem jobs(0);
    SetEngineJobs(&jobs);
    jobs.Submit([] {});   // before the frame: not part of this frame's job time
    Profiler& p = Prof();
    p.enabled = true;
    p.paused = false;
    p.BeginFrame();
    jobs.Submit([] {});
    p.EndFrame(16.0);
    p.enabled = false;
    SetEngineJobs(nullptr);
    bool found = false;
    const Profiler::Frame& frame = p.FrameAt(p.Count() - 1);
    for (const auto& b : frame.buckets)
        if (b.name == "Jobs" && b.calls >= 1) found = true;
    CHECK(found);

    // Job time is shown, but it is not added into the frame's CPU time: a 16 ms frame with two
    // 30 ms jobs used to report 60 ms.
    {
        RUN_SUITE("profiler_jobs: cpu total");
        JobSystem inl(0);
        SetEngineJobs(&inl);
        p.enabled = true;
        p.BeginFrame();
        inl.Submit([] { Spin(30.0); });
        inl.Submit([] { Spin(30.0); });
        p.EndFrame(16.0);
        const Profiler::Frame& f = p.FrameAt(p.Count() - 1);
        CHECK(JobsMs(f) >= 50.0);
        CHECK(f.cpu < 10.0);
        CHECK(p.cpuMs < 10.0);

        // A job that ran while the profiler was off is not counted in the first frame after it is turned back on.
        p.enabled = false;
        inl.Submit([] { Spin(40.0); });
        Spin(1100.0);
        p.enabled = true;
        p.BeginFrame();
        p.EndFrame(16.0);
        CHECK(JobsMs(p.FrameAt(p.Count() - 1)) < 5.0);
        p.enabled = false;
        SetEngineJobs(nullptr);
    }
    // The editor and the player must not install an inline JobSystem(0) as the engine's pool to
    // feed the profiler's Jobs row: that ran navmesh bakes, decode, physics and skinning on the UI
    // thread. The row reads whatever pool is already running (NoteRunningJobs).
    {
        RUN_SUITE("profiler_jobs: no inline engine pool");
        for (const char* rel : {"/editor/main.cpp", "/player/main.cpp"}) {
            std::ifstream in(std::string(OKAY_SOURCE_DIR) + rel);
            CHECK(in.good());
            std::string line;
            int bad = 0;
            while (std::getline(in, line)) {
                const std::size_t code = line.find("//");
                const std::string body = code == std::string::npos ? line : line.substr(0, code);
                if (body.find("JobSystem(0)") != std::string::npos) ++bad;
            }
            CHECK(bad == 0);
        }
    }
    TEST_MAIN_RESULT();
}
