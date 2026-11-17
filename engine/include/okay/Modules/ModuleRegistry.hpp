#pragma once
#include "okay/Modules/ModuleManifest.hpp"
#include <string>
#include <vector>

namespace okay {

/// The list of engine modules in this build and whether each is on.
///
/// The build (CMake option OKAY_MODULES, cmake/OkayModules.cmake) generates modules_enabled.cpp, which
/// adds every manifest under engine/modules/ and marks the ones that are on. Everything that wants to know
/// "which modules exist, what do they add, what do they need" asks here instead of keeping its own list.
///
/// Threading: modules are added once, on first use, before any other thread runs engine code. There is
/// no lock (the Windows build cannot use <mutex>); only read afterwards, or call from one thread.
class ModuleRegistry;

/// Handed to a module's Register_<name> function (engine/modules/<name>/register.cpp).
struct ModuleContext {
    ModuleRegistry& registry;
};
using ModuleRegisterFn = void (*)(ModuleContext&);

class ModuleRegistry {
public:
    struct Info {
        ModuleManifest manifest;
        bool enabled = true;
    };

    /// The registry of this program. The first call adds the generated modules.
    static ModuleRegistry& Get();
    /// An empty registry that does not load the generated modules (tests, tools).
    static ModuleRegistry Empty() { return ModuleRegistry(); }

    /// Add (or replace, by name) a module. Used by the generated file and by tests.
    void Add(const ModuleManifest& m, bool enabled);
    /// Parse manifest text and add it. False and `err` on a parse error.
    bool AddText(const std::string& text, bool enabled, std::string* err = nullptr);
    void Clear() { mods_.clear(); }

    const Info* Find(const std::string& name) const;
    const std::vector<Info>& All() const { return mods_; }
    bool IsEnabled(const std::string& name) const;

    /// Module that owns a component type / a scene record / a provided contract, or nullptr.
    const Info* ModuleOfComponent(const std::string& type) const;
    const Info* ModuleOfRecord(const std::string& record) const;
    const Info* ModuleProviding(const std::string& contract) const;
    /// False only when the type belongs to a module that is switched off. Unknown types count as on.
    bool ComponentEnabled(const std::string& type) const;
    bool RecordEnabled(const std::string& record) const;

    /// Modules in dependency order (a module comes after everything it requires). A cycle is broken at
    /// the edge that closes it; Validate() reports it.
    std::vector<std::string> DependencyOrder() const;
    /// Everything `name` needs, directly and through other modules (not including `name`).
    std::vector<std::string> TransitiveRequires(const std::string& name) const;
    /// Modules that need `name`, directly or through others.
    std::vector<std::string> RequiredBy(const std::string& name) const;
    /// Required modules of `name` that are currently off.
    std::vector<std::string> MissingRequires(const std::string& name) const;

    /// Turn a wanted set into a complete one: adds core modules and every transitive requirement.
    /// Names that do not exist go to `unknown` (when given) and are skipped. `added` receives the modules
    /// that were pulled in only because something else needs them. Result is in dependency order.
    std::vector<std::string> Resolve(const std::vector<std::string>& wanted,
                                     std::vector<std::string>* added = nullptr,
                                     std::vector<std::string>* unknown = nullptr) const;

    /// A module's register function: the build links engine/modules/<name>/register.cpp only for modules that
    /// are on, and the generated file hands each one over here. RunRegisterHooks calls the hooks of enabled
    /// modules in dependency order (a module registers after everything it requires). Safe to call twice:
    /// each hook runs once.
    void SetRegisterHook(const std::string& name, ModuleRegisterFn fn);
    void RunRegisterHooks();

    /// Make exactly Resolve(names) the enabled set.
    void SetEnabledSet(const std::vector<std::string>& names);

    /// Problems in the module list itself: unknown `requires`, a cycle, a component or record claimed by
    /// two modules, a core module that requires a non-core one. Empty = fine.
    std::vector<std::string> Validate() const;

    /// The enabled modules as a sorted comma list ("ai_behavior,audio,..."), what two peers compare in the join handshake.
    std::string NetSignature() const;
    /// "" when `other` (a NetSignature) names the same modules as this registry, else a plain sentence saying what the other
    /// side is missing and what it has extra. `other` is the remote side; this registry is the local one.
    std::string DescribeMismatch(const std::string& other) const;

    /// "39 modules, 39 on" (for logs and the startup line).
    std::string Summary() const;

private:
    ModuleRegistry() = default;
    std::vector<Info> mods_;
    struct Hook { std::string name; ModuleRegisterFn fn; bool ran; };
    std::vector<Hook> hooks_;
};

namespace detail {
/// Defined in the generated modules_enabled.cpp: adds every manifest in the build.
void RegisterGeneratedModules(ModuleRegistry& r);
/// The OKAY_MODULES value the build was configured with ("all" by default).
const char* ConfiguredModuleSelection();
} // namespace detail

} // namespace okay
