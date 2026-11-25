// Review fix for PR #1218: the first call to ModuleRegistry::Get() builds the registry and runs the
// module hooks. Workers that build a visual-script graph (MakeNode) or look a node up before the main
// thread has touched the registry must wait for that build, never race it or see it half done.
// Nothing here touches the registry before the threads start.
#include "test_framework.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/VisualScript/Nodes.hpp"
#include "okay/VisualScript/VsNodeRegistry.hpp"
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

using namespace okay;

int main() {
    RUN_SUITE("module registry: first use from many threads");
    constexpr int kThreads = 8;
    std::atomic<bool> go{false};
    std::atomic<int> madeNodes{0}, foundEntries{0}, sameRegistry{0}, sawWeapons{0};
    std::atomic<ModuleRegistry*> first{nullptr};
    std::vector<std::thread> workers;
    for (int i = 0; i < kThreads; ++i) {
        workers.emplace_back([&, i] {
            while (!go.load(std::memory_order_acquire)) {}
            if (i % 2 == 0) {
                // A worker that only looks a node up (the add-node list does this) must still see the
                // nodes the weapons hook registered, even when it is the first caller.
                if (vs::FindRegisteredVsNode("FireWeapon")) ++foundEntries;
                if (vs::MakeNode("ReloadWeapon", {}, nullptr)) ++madeNodes;
            } else {
                if (vs::MakeNode("SwingWeapon", {}, nullptr)) ++madeNodes;
                if (vs::FindRegisteredVsNode("WeaponAmmo")) ++foundEntries;
            }
            ModuleRegistry* r = &ModuleRegistry::Get();
            ModuleRegistry* expected = nullptr;
            first.compare_exchange_strong(expected, r);
            if (first.load() == r) ++sameRegistry;
            if (r->IsEnabled("weapons")) ++sawWeapons;
        });
    }
    go.store(true, std::memory_order_release);
    for (auto& t : workers) t.join();

    CHECK(madeNodes.load() == kThreads);
    CHECK(foundEntries.load() == kThreads);
    CHECK(sameRegistry.load() == kThreads);
    CHECK(sawWeapons.load() == kThreads);
    // The hooks ran once: each node type is registered once, not once per thread.
    auto types = vs::RegisteredVsNodeTypes(false);
    int fire = 0;
    for (const std::string& t : types) if (t == "FireWeapon") ++fire;
    CHECK(fire == 1);
    std::cout << kThreads << " threads: nodes made " << madeNodes.load() << ", entries found " << foundEntries.load()
              << ", one registry " << sameRegistry.load() << ", FireWeapon registered " << fire << "x\n";
    TEST_MAIN_RESULT();
}
