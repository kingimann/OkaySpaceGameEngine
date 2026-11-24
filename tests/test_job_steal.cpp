// A worker that is busy can have its queued jobs taken by another worker.
#include "test_framework.hpp"
#include "okay/Core/Jobs.hpp"
#include <atomic>
#include <chrono>
#include <iostream>

using namespace okay;

int main() {
    RUN_SUITE("job_steal");
    if (!JobSystem::ThreadsAvailable()) {
        std::cout << "skipped: this build has no worker threads\n";
        TEST_MAIN_RESULT();
    }
    JobSystem jobs(2);
    if (jobs.WorkerCount() < 2) {
        std::cout << "skipped: the pool did not start 2 workers\n";
        TEST_MAIN_RESULT();
    }
    std::atomic<int> phase{0};
    std::atomic<int> children{0};
    JobHandle parent = jobs.Submit([&] {
        for (int i = 0; i < 4; ++i)
            jobs.Submit([&] { children.fetch_add(1, std::memory_order_relaxed); });
        phase.store(1, std::memory_order_release);
        while (phase.load(std::memory_order_acquire) < 2) {}
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (phase.load(std::memory_order_acquire) < 1 && std::chrono::steady_clock::now() < deadline) {}
    while (children.load(std::memory_order_acquire) < 4 && std::chrono::steady_clock::now() < deadline) {}
    phase.store(2, std::memory_order_release);
    jobs.Wait(parent);
    CHECK(children.load() == 4);
    CHECK(jobs.Stats().stolen >= 1);
    TEST_MAIN_RESULT();
}
