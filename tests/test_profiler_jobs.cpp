// The profiler frame records job-system time when a job system is already running.
#include "test_framework.hpp"
#include "okay/Core/Profiler.hpp"
#include "okay/Core/Jobs.hpp"

using namespace okay;

int main() {
    RUN_SUITE("profiler_jobs");
    JobSystem jobs(0);
    SetEngineJobs(&jobs);
    jobs.Submit([] {});   // before the profiler is on: must not land in the first frame
    Profiler& p = Prof();
    p.enabled = true;
    p.paused = false;
    p.BeginFrame();
    jobs.Submit([] {});
    p.EndFrame(16.0);
    bool found = false;
    const Profiler::Frame& frame = p.FrameAt(p.Count() - 1);
    for (const auto& b : frame.buckets)
        if (b.name == "Jobs" && b.calls >= 1) found = true;
    CHECK(found);
    // A quiet frame still names the Jobs row, so the profiler can show it at zero.
    p.BeginFrame();
    p.EndFrame(16.0);
    p.enabled = false;
    bool quiet = false;
    const Profiler::Frame& q = p.FrameAt(p.Count() - 1);
    for (const auto& b : q.buckets)
        if (b.name == "Jobs") quiet = true;
    CHECK(quiet);
    SetEngineJobs(nullptr);
    TEST_MAIN_RESULT();
}
