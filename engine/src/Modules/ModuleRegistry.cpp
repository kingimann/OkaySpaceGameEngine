#include "okay/Modules/ModuleRegistry.hpp"
#include <algorithm>
#include <atomic>
#include <map>
#include <set>

namespace okay {

namespace {
bool Has(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}
} // namespace

ModuleRegistry& ModuleRegistry::Get() {
    // Not a function-local static: a hook may ask for the registry again while it runs. The pointer is
    // published before the hooks, so that re-entry sees the half-filled registry. phase makes a second
    // thread wait instead of building a second registry (MakeNode can run on a worker before the main
    // thread has touched this).
    static std::atomic<ModuleRegistry*> published{nullptr};
    static std::atomic<int> phase{0};   // 0 empty, 1 building, 2 ready
    static thread_local bool initializing = false;
    if (phase.load(std::memory_order_acquire) == 2) return *published.load(std::memory_order_acquire);
    if (initializing) return *published.load(std::memory_order_relaxed);
    int expected = 0;
    if (phase.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
        initializing = true;
        auto* created = new ModuleRegistry();   // lives for the whole run, never destroyed
        published.store(created, std::memory_order_release);
        detail::RegisterGeneratedModules(*created);
        created->RunRegisterHooks();
        phase.store(2, std::memory_order_release);
        initializing = false;
        return *created;
    }
    while (phase.load(std::memory_order_acquire) != 2) {}
    return *published.load(std::memory_order_acquire);
}

void ModuleRegistry::Add(const ModuleManifest& m, bool enabled) {
    Info info;
    info.manifest = m;
    info.enabled = enabled || m.core;
    for (auto& e : mods_)
        if (e.manifest.name == m.name) { e = info; return; }
    mods_.push_back(info);
}

bool ModuleRegistry::AddText(const std::string& text, bool enabled, std::string* err) {
    ModuleManifest m;
    if (!ModuleManifest::Parse(text, m, err)) return false;
    Add(m, enabled);
    return true;
}

const ModuleRegistry::Info* ModuleRegistry::Find(const std::string& name) const {
    for (auto& e : mods_) if (e.manifest.name == name) return &e;
    return nullptr;
}

bool ModuleRegistry::IsEnabled(const std::string& name) const {
    const Info* i = Find(name);
    return i && i->enabled;
}

std::vector<std::string> ModuleRegistry::DependencyOrder() const {
    std::vector<std::string> order;
    std::set<std::string> done, visiting;
    struct Rec {
        const ModuleRegistry& r; std::vector<std::string>& order; std::set<std::string>& done; std::set<std::string>& visiting;
        void Visit(const std::string& n) {
            if (done.count(n) || visiting.count(n)) return;   // second case = cycle: skip the closing edge
            const Info* i = r.Find(n);
            if (!i) return;
            visiting.insert(n);
            for (auto& q : i->manifest.requires_) Visit(q);
            visiting.erase(n);
            done.insert(n);
            order.push_back(n);
        }
    } rec{*this, order, done, visiting};
    for (auto& e : mods_) rec.Visit(e.manifest.name);
    return order;
}

std::vector<std::string> ModuleRegistry::TransitiveRequires(const std::string& name) const {
    std::vector<std::string> out;
    std::vector<std::string> stack{name};
    std::set<std::string> seen{name};
    while (!stack.empty()) {
        std::string n = stack.back(); stack.pop_back();
        const Info* i = Find(n);
        if (!i) continue;
        for (auto& q : i->manifest.requires_)
            if (seen.insert(q).second) { out.push_back(q); stack.push_back(q); }
    }
    return out;
}

const ModuleRegistry::Info* ModuleRegistry::ModuleOfComponent(const std::string& type) const {
    for (auto& e : mods_) if (Has(e.manifest.components, type)) return &e;
    return nullptr;
}

std::vector<std::string> ModuleRegistry::RequiredBy(const std::string& name) const {
    std::vector<std::string> out;
    for (auto& e : mods_)
        if (e.manifest.name != name && Has(TransitiveRequires(e.manifest.name), name)) out.push_back(e.manifest.name);
    return out;
}

std::vector<std::string> ModuleRegistry::Resolve(const std::vector<std::string>& wanted,
                                                 std::vector<std::string>* added,
                                                 std::vector<std::string>* unknown) const {
    std::set<std::string> on;
    for (auto& e : mods_) if (e.manifest.core) on.insert(e.manifest.name);
    for (auto& w : wanted) {
        if (!Find(w)) { if (unknown && !Has(*unknown, w)) unknown->push_back(w); continue; }
        on.insert(w);
    }
    std::set<std::string> asked = on;
    for (auto& n : std::vector<std::string>(on.begin(), on.end()))
        for (auto& q : TransitiveRequires(n))
            if (Find(q)) on.insert(q);
    if (added)
        for (auto& n : on)
            if (!asked.count(n)) added->push_back(n);
    std::vector<std::string> out;
    for (auto& n : DependencyOrder()) if (on.count(n)) out.push_back(n);
    return out;
}

void ModuleRegistry::SetRegisterHook(const std::string& name, ModuleRegisterFn fn) {
    for (auto& h : hooks_)
        if (h.name == name) { h.fn = fn; h.ran = false; return; }
    hooks_.push_back({name, fn, false});
}

void ModuleRegistry::RunRegisterHooks() {
    ModuleContext ctx{*this};
    for (auto& n : DependencyOrder()) {
        if (!IsEnabled(n)) continue;
        for (auto& h : hooks_)
            if (h.name == n && !h.ran && h.fn) { h.ran = true; h.fn(ctx); }
    }
}

std::vector<std::string> ModuleRegistry::Validate() const {
    std::vector<std::string> p;
    std::map<std::string, std::string> compOwner, recOwner;
    for (auto& e : mods_) {
        const ModuleManifest& m = e.manifest;
        for (auto& q : m.requires_) {
            const Info* r = Find(q);
            if (!r) p.push_back("module '" + m.name + "' requires unknown module '" + q + "'");
            else if (m.core && !r->manifest.core)
                p.push_back("core module '" + m.name + "' requires non-core module '" + q + "'");
        }
        for (auto& q : m.requires_)
            if (Has(TransitiveRequires(q), m.name)) p.push_back("modules '" + m.name + "' and '" + q + "' require each other (cycle)");
        for (auto& c : m.components) {
            auto it = compOwner.find(c);
            if (it != compOwner.end() && it->second != m.name)
                p.push_back("component '" + c + "' is claimed by '" + it->second + "' and '" + m.name + "'");
            else compOwner[c] = m.name;
        }
        for (auto& c : m.serializers) {
            auto it = recOwner.find(c);
            if (it != recOwner.end() && it->second != m.name)
                p.push_back("scene record '" + c + "' is claimed by '" + it->second + "' and '" + m.name + "'");
            else recOwner[c] = m.name;
        }
    }
    return p;
}

std::string ModuleRegistry::Summary() const {
    size_t on = 0;
    for (auto& e : mods_) if (e.enabled) ++on;
    return std::to_string(mods_.size()) + " modules, " + std::to_string(on) + " on";
}

} // namespace okay
