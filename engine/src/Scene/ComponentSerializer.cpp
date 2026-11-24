#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include "SceneTextDetail.hpp"
#include <ostream>
#include <typeindex>
#include <unordered_map>
#include <atomic>
#include <cctype>
#include "okay/Debug/DebugUtil.hpp"   // debugutil::SpinLock (no <mutex> on MinGW win32 threads, rule 21a)
#include <typeinfo>
#include <vector>

namespace okay {

namespace {
std::atomic<std::uint64_t> g_serializerGeneration{0};   // moves whenever an entry is registered or replaced
struct Table {
    debugutil::SpinLock mutex;
    std::vector<std::shared_ptr<const ComponentSerializerRegistry::Entry>> entries;
    std::unordered_map<std::string, std::type_index> genericTypes;   // record -> type, for the entries RegisterFromProperties made
};
Table& GetTable() { static Table t; return t; }

void AddEntry(Table& t, ComponentSerializerRegistry::Entry e) {
    debugutil::SpinGuard lock(t.mutex);
    auto sp = std::make_shared<const ComponentSerializerRegistry::Entry>(std::move(e));
    g_serializerGeneration.fetch_add(1, std::memory_order_relaxed);
    for (auto& cur : t.entries) if (cur->name == sp->name) { cur = sp; return; }
    t.entries.push_back(sp);
}
void EnsureBuiltins(Table&) {
    // Once, thread-safe: a function-local static is initialised exactly once (C++11), with no <mutex> (rule 21a).
    static const bool registered = (detail::RegisterBuiltinComponentSerializers(), true);
    (void)registered;
}
} // namespace

void ComponentSerializerRegistry::Register(Entry entry) {
    if (entry.name.empty() || !entry.write || (!entry.read && !entry.readWithNotes)) return;
    AddEntry(GetTable(), std::move(entry));
}

void ComponentSerializerRegistry::Unregister(const std::string& name) {
    Table& t = GetTable();
    EnsureBuiltins(t);   // before the lock, and so a later first Find does not see a half-built table
    debugutil::SpinGuard lock(t.mutex);
    for (auto it = t.entries.begin(); it != t.entries.end(); ++it)
        if ((*it)->name == name) {
            t.entries.erase(it);
            g_serializerGeneration.fetch_add(1, std::memory_order_relaxed);
            return;
        }
}

std::shared_ptr<const ComponentSerializerRegistry::Entry> ComponentSerializerRegistry::Find(const std::string& name) {
    Table& t = GetTable();
    EnsureBuiltins(t);
    debugutil::SpinGuard lock(t.mutex);
    for (auto& e : t.entries) if (e->name == name) return e;
    return nullptr;
}

void ComponentSerializerRegistry::WriteAll(const GameObject& go, std::ostream& out) {
    Table& t = GetTable();
    EnsureBuiltins(t);
    std::vector<std::shared_ptr<const Entry>> copy;
    { debugutil::SpinGuard lock(t.mutex); copy = t.entries; }
    for (auto& e : copy) if (!e->inChain) e->write(go, out);
}

namespace detail {
/// Moves whenever a component serializer is registered: the records an object writes (so the order they load in) change with it.
std::uint64_t ComponentSerializerGeneration() { return g_serializerGeneration.load(std::memory_order_relaxed); }
} // namespace detail

// ---- The generic path ---------------------------------------------------------------------------------------
// The fields themselves are written and read in SceneSerializer.cpp (WritePropertyFields / ReadPropertyFields), next
// to the quoting rules of the rest of the file.
bool ComponentSerializerRegistry::RegisterFromProperties(const std::string& typeName, std::string* error) {
    auto fail = [&](const std::string& why) { if (error) *error = why; return false; };
    if (error) error->clear();
    std::shared_ptr<const ComponentTypeInfo> info = PropertyRegistry::FindByName(typeName);
    if (!info) return fail("no PropertyRegistry entry named '" + typeName + "'");
    if (info->objectLevel || !info->add) return fail("'" + info->name + "' is not a component that can be added");
    const std::string& rec = info->recordName;
    if (rec.empty()) return fail("'" + info->name + "' has no record name");
    for (char ch : rec)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_'))
            return fail("record name '" + rec + "' is not lower-case letters, digits and '_'");
    // A record the built-in chain reads is never taken over: the chain would read it first, and old files keep it.
    if (scenetext::IsBuiltinObjectRecord(rec)) return fail("record '" + rec + "' belongs to the built-in serializer");
    Table& t = GetTable();
    {
        debugutil::SpinGuard lock(t.mutex);
        for (const auto& e : t.entries)
            if (e->name == rec) {
                auto own = t.genericTypes.find(rec);
                if (own == t.genericTypes.end() || own->second != info->type)
                    return fail("record '" + rec + "' is already used by another serializer");
            }
        t.genericTypes.insert_or_assign(rec, info->type);
    }
    Entry e;
    e.name = rec;
    e.write = [info](const GameObject& go, std::ostream& out) {
        for (const auto& c : go.Components()) {
            if (!c || std::type_index(typeid(*c)) != info->type) continue;
            out << "  " << info->recordName;
            WritePropertyFields(*c, *info, out);
            out << "\n";
        }
    };
    auto readNoted = [info](GameObject& go, std::istream& in, std::vector<std::string>& notes) {
        Component* c = info->add(go);
        if (!c) { notes.push_back("could not add a " + info->name + " for record '" + info->recordName + "'"); return; }
        std::vector<std::string> mine;
        ReadPropertyFields(*c, *info, in, &mine);
        for (auto& n : mine) notes.push_back(info->recordName + ": " + n);
    };
    e.readWithNotes = readNoted;
    e.read = [readNoted](GameObject& go, std::istream& in) { std::vector<std::string> ignored; readNoted(go, in, ignored); };
    AddEntry(t, std::move(e));
    return true;
}

std::string ComponentSerializerRegistry::TypeKey(const Component& c) {
    const std::string raw = typeid(c).name();
    // Itanium names carry length-prefixed identifiers ("N4okay14SpriteRendererE"):
    // take the last one. MSVC names read "class okay::SpriteRenderer": take what
    // follows the last "::" or space.
    std::string last;
    for (std::size_t i = 0; i < raw.size();) {
        if (std::isdigit((unsigned char)raw[i])) {
            std::size_t n = 0;
            while (i < raw.size() && std::isdigit((unsigned char)raw[i])) n = n * 10 + (std::size_t)(raw[i++] - '0');
            if (n > 0 && i + n <= raw.size()) { last = raw.substr(i, n); i += n; continue; }
            break;
        }
        ++i;
    }
    if (!last.empty()) return last;
    std::size_t cut = raw.find_last_of(": ");
    return cut == std::string::npos ? raw : raw.substr(cut + 1);
}

} // namespace okay
