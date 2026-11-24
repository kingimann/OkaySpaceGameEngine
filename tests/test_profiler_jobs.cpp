// The profiler frame records job-system time when a job system is already running.
#include "test_framework.hpp"
#include "okay/Core/Profiler.hpp"
#include "okay/Core/Jobs.hpp"

using namespace okay;

int main() {
    RUN_SUITE("profiler_jobs");
    JobSystem jobs(0);
    SetEngineJobs(&jobs);
    jobs.Submit([] {});
    Profiler& p = Prof();
    p.enabled = true;
    p.paused = false;
    p.BeginFrame();
    p.EndFrame(16.0);
    p.enabled = false;
    SetEngineJobs(nullptr);
    bool found = false;
    const Profiler::Frame& frame = p.FrameAt(p.Count() - 1);
    for (const auto& b : frame.buckets)
        if (b.name == "Jobs" && b.calls >= 1) found = true;
    CHECK(found);
    TEST_MAIN_RESULT();
}
