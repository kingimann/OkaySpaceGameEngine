// editor/WorkerCall: one call on its own thread, collected on a later frame. The editor's update check (CheckLatest: up to 20 s to
// connect and 60 s per read, three tries) and the update download used to run inside the frame when the person chose Engine >
// Check for Updates or "Update OkaySpace" (the owner's okay_editor_perf.log, 2026-11-25). The bound here is not a time: the call is
// held on a gate that only this thread opens, so Start returning at all proves the frame does not wait for the call.
#include "test_framework.hpp"
#include "WorkerCall.hpp"

#include <atomic>
#include <memory>
#include <string>

using oked::WorkerCall;

namespace {
void WaitFor(const std::atomic<int>& v, int want) { while (v.load() != want) {} }
}

int main() {
    RUN_SUITE("Start returns while the call is still running; Collect is true once, after it finished");
    {
        WorkerCall call;
        auto gate = std::make_shared<std::atomic<int>>(0);
        auto out = std::make_shared<std::string>();
        CHECK(call.Start([gate, out] { WaitFor(*gate, 1); *out = "v9.9.9"; }, "okay-test-call"));   // would hang here if it ran inline
        CHECK(call.Busy());
        CHECK(!call.Collect());                                   // still held on the gate
        CHECK(!call.Start([] {}, "second"));                      // one at a time
        gate->store(1);
        int frames = 0;
        while (!call.Collect()) ++frames;                         // the editor asks once a frame
        CHECK(*out == "v9.9.9");
        CHECK(!call.Busy() && !call.Collect());                   // only once
        CHECK(call.Start([out] { *out = "again"; }, "third"));    // free for the next one
        while (!call.Collect()) {}
        CHECK(*out == "again");
    }

    RUN_SUITE("dropping a running call lets it finish on its own (closing the editor during a check)");
    {
        auto gate = std::make_shared<std::atomic<int>>(0);
        auto done = std::make_shared<std::atomic<int>>(0);
        {
            WorkerCall call;
            CHECK(call.Start([gate, done] { WaitFor(*gate, 1); done->store(1); }, "okay-test-drop"));
        }   // destroyed while the call waits
        gate->store(1);
        WaitFor(*done, 1);
        CHECK(done->load() == 1);
        WorkerCall call;
        CHECK(call.Start([] { throw 1; }, "okay-test-throw"));   // a throwing call still counts as finished
        while (!call.Collect()) {}
        CHECK(!call.Busy());
    }
    TEST_MAIN_RESULT();
}
