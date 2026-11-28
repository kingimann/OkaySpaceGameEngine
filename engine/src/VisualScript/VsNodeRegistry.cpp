#include "okay/VisualScript/VsNodeRegistry.hpp"
#include "okay/VisualScript/NodesCombat.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Debug/DebugUtil.hpp"
#include <atomic>
#include <deque>
#include <utility>

namespace okay::vs {

namespace {
// A deque: entries never move, so a pointer handed out stays good while more types are added.
std::deque<VsNodeEntry>& Table() {
    static std::deque<VsNodeEntry> t;
    return t;
}
debugutil::SpinLock& Lock() {
    static debugutil::SpinLock l;
    return l;
}
std::atomic<int> g_revision{0};

VsNodeEntry* FindLocked(const std::string& type) {
    for (VsNodeEntry& e : Table()) if (e.type == type) return &e;
    return nullptr;
}
NodeSignature SignatureOf(const VsNodeInfo& info) {
    NodeSignature s;
    s.execIn = info.execIn;
    for (const VsPinSpec& p : info.ins) s.ins += (char)p.type;
    for (const VsPinSpec& p : info.outs) s.outs += (char)p.type;
    return s;
}

std::string Arg0(const std::vector<std::string>& args, const char* fallback) {
    return args.empty() || args[0].empty() ? std::string(fallback) : args[0];
}
std::unique_ptr<VsNode> MakeSwing(const std::vector<std::string>&, std::string*) {
    return std::make_unique<SwingWeaponNode>();
}
std::unique_ptr<VsNode> MakeFire(const std::vector<std::string>&, std::string*) {
    return std::make_unique<FireWeaponNode>();
}
std::unique_ptr<VsNode> MakeReload(const std::vector<std::string>&, std::string*) {
    return std::make_unique<ReloadWeaponNode>();
}
std::unique_ptr<VsNode> MakeAmmo(const std::vector<std::string>&, std::string*) {
    return std::make_unique<WeaponAmmoNode>();
}
std::unique_ptr<VsNode> MakeSetWeapon(const std::vector<std::string>& args, std::string*) {
    return std::make_unique<SetWeaponNode>(Arg0(args, "sword"));
}
} // namespace

void RegisterVsNode(std::string type, std::string module,
                    std::unique_ptr<VsNode> (*make)(const std::vector<std::string>&, std::string*)) {
    debugutil::SpinGuard guard(Lock());
    const int rev = ++g_revision;
    if (VsNodeEntry* e = FindLocked(type)) {
        e->module = std::move(module); e->make = make; e->removed = false; e->revision = rev;
        return;
    }
    VsNodeEntry e;
    e.type = std::move(type); e.module = std::move(module); e.make = make; e.revision = rev;
    Table().push_back(std::move(e));
}

void RegisterNodeType(std::string type, VsNodeInfo info, VsNodeFactory make) {
    okay::ModuleRegistry::Get();   // module hooks register their nodes first (never while the lock is held)
    debugutil::SpinGuard guard(Lock());
    const int rev = ++g_revision;
    VsNodeEntry* e = FindLocked(type);
    if (!e) {
        Table().emplace_back();
        e = &Table().back();
        e->type = std::move(type);
    }
    e->module.clear();
    e->make = std::move(make);
    e->custom = true;
    e->sig = SignatureOf(info);
    e->info = std::move(info);
    e->revision = rev;
    e->removed = false;
}

bool UnregisterNodeType(const std::string& type) {
    debugutil::SpinGuard guard(Lock());
    VsNodeEntry* e = FindLocked(type);
    if (!e || e->removed) return false;
    e->removed = true;
    e->revision = ++g_revision;
    return true;
}

const VsNodeEntry* FindRegisteredVsNode(const std::string& type) {
    // The module hooks fill the table; the first caller (on any thread) must see them done.
    okay::ModuleRegistry::Get();
    debugutil::SpinGuard guard(Lock());
    const VsNodeEntry* e = FindLocked(type);
    return e && !e->removed ? e : nullptr;
}

bool MakeRegisteredVsNode(const std::string& type, const std::vector<std::string>& args, std::string* err,
                          std::unique_ptr<VsNode>& out) {
    okay::ModuleRegistry& mods = okay::ModuleRegistry::Get();
    VsNodeFactory make;
    std::string module;
    {
        debugutil::SpinGuard guard(Lock());
        const VsNodeEntry* e = FindLocked(type);
        if (!e || e->removed) return false;
        make = e->make;   // a copy: the factory runs outside the lock (it may load a script)
        module = e->module;
    }
    out.reset();
    if (!module.empty() && !mods.IsEnabled(module)) {
        if (err) *err = "the " + module + " module is off";
        return true;
    }
    if (!make) {
        if (err) *err = "node type '" + type + "' has no factory";
        return true;
    }
    out = make(args, err);
    return true;
}

bool VsNodeSuppressed(const std::string& type) {
    const VsNodeEntry* e = FindRegisteredVsNode(type);
    if (!e || e->module.empty()) return false;
    return !okay::ModuleRegistry::Get().IsEnabled(e->module);
}

std::vector<std::string> RegisteredVsNodeTypes(bool enabledOnly) {
    okay::ModuleRegistry& mods = okay::ModuleRegistry::Get();
    std::vector<std::pair<std::string, std::string>> rows;   // type, module
    {
        debugutil::SpinGuard guard(Lock());
        for (const VsNodeEntry& e : Table()) if (!e.removed) rows.push_back({e.type, e.module});
    }
    std::vector<std::string> out;
    for (const auto& r : rows) {
        if (enabledOnly && !r.second.empty() && !mods.IsEnabled(r.second)) continue;
        out.push_back(r.first);
    }
    return out;
}

std::vector<std::string> CustomVsNodeTypes() {
    debugutil::SpinGuard guard(Lock());
    std::vector<std::string> out;
    for (const VsNodeEntry& e : Table()) if (e.custom && !e.removed) out.push_back(e.type);
    return out;
}

bool CustomVsNodeInfo(const std::string& type, VsNodeInfo& out) {
    debugutil::SpinGuard guard(Lock());
    const VsNodeEntry* e = FindLocked(type);
    if (!e || !e->custom || e->removed) return false;
    out = e->info;
    return true;
}

const NodeSignature* CustomVsNodeSignature(const std::string& type) {
    debugutil::SpinGuard guard(Lock());
    const VsNodeEntry* e = FindLocked(type);
    return e && e->custom && !e->removed ? &e->sig : nullptr;
}

int VsNodeRegistryRevision() { return g_revision.load(); }

void RegisterWeaponVsNodes() {
    RegisterVsNode("SwingWeapon", "weapons", &MakeSwing);
    RegisterVsNode("FireWeapon", "weapons", &MakeFire);
    RegisterVsNode("ReloadWeapon", "weapons", &MakeReload);
    RegisterVsNode("WeaponAmmo", "weapons", &MakeAmmo);
    RegisterVsNode("SetWeapon", "weapons", &MakeSetWeapon);
}

} // namespace okay::vs
