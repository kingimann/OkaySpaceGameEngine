#pragma once
// ---------------------------------------------------------------------------
// Run order per component type, set by the project (Edit > Project Settings > Run order) without touching code.
//
// Every component has Component::ExecutionOrder() (0 for most; IK returns 100). A project can override that for a whole
// TYPE with three plain words: "Run early" (before everything else), "Normal" (the component's own number) and
// "Run late" (after everything else). Each frame the scene calls Update, LateUpdate and FixedUpdate in this order; inside
// one group components run in the order they were added, so a frame is the same on every run.
//
// The settings are process-wide, main thread only, and saved in project.okayproj as `run_order_<TypeName>=early|late`
// lines (see ProjectLists). Written from our own design; no <mutex> / <thread>.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace okay {

class Component;

enum class RunWhen { Early = -1, Normal = 0, Late = 1 };

class ExecutionOrderSettings {
public:
    /// The order values "Run early" / "Run late" stand for (beyond any number a component returns on its own).
    static constexpr int kEarly = -1000;
    static constexpr int kLate = 1000;
    /// Set when `typeName` (the C++ type name as ComponentSerializerRegistry::TypeKey gives it, e.g. "Rigidbody2D") runs.
    /// Normal removes the setting. Returns false for an empty name or one with '=', ' ' or a newline in it.
    static bool Set(const std::string& typeName, RunWhen when);
    static RunWhen Get(const std::string& typeName);
    /// Every type with a setting other than Normal, sorted by name.
    static std::vector<std::pair<std::string, RunWhen>> All();
    /// Forget every setting.
    static void Clear();
    /// True while at least one type has a setting (lets the scene skip all of this when none does).
    static bool Any();
    /// Moves on every change, so a scene knows to sort its update list again.
    static std::uint64_t Version();
    /// The order value `c` runs at: the project's setting when its type has one, otherwise c.ExecutionOrder().
    static int Effective(const Component& c);
    /// "Run early" / "Normal" / "Run late".
    static const char* Label(RunWhen when);
};

} // namespace okay
