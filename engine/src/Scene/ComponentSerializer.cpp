#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Scene/Component.hpp"
#include <atomic>
#include <cctype>
#include <mutex>
#include <typeinfo>
#include <vector>

namespace okay {

namespace {
std::atomic<std::uint64_t> g_serializerGeneration{0};   // moves whenever an entry is registered or replaced
struct Table {
    std::mutex mutex;
    std::vector<std::shared_ptr<const ComponentSerializerRegistry::Entry>> entries;
    std::once_flag builtinsOnce;
};
Table& GetTable() { static Table t; return t; }

void AddEntry(Table& t, ComponentSerializerRegistry::Entry e) {
    std::lock_guard<std::mutex> lock(t.mutex);
    auto sp = std::make_shared<const ComponentSerializerRegistry::Entry>(std::move(e));
    g_serializerGeneration.fetch_add(1, std::memory_order_relaxed);
    for (auto& cur : t.entries) if (cur->name == sp->name) { cur = sp; return; }
    t.entries.push_back(sp);
}
void EnsureBuiltins(Table& t) {
    std::call_once(t.builtinsOnce, [] { detail::RegisterBuiltinComponentSerializers(); });
}
} // namespace

void ComponentSerializerRegistry::Register(Entry entry) {
    if (entry.name.empty() || !entry.write || !entry.read) return;
    AddEntry(GetTable(), std::move(entry));
}

std::shared_ptr<const ComponentSerializerRegistry::Entry> ComponentSerializerRegistry::Find(const std::string& name) {
    Table& t = GetTable();
    EnsureBuiltins(t);
    std::lock_guard<std::mutex> lock(t.mutex);
    for (auto& e : t.entries) if (e->name == name) return e;
    return nullptr;
}

void ComponentSerializerRegistry::WriteAll(const GameObject& go, std::ostream& out) {
    Table& t = GetTable();
    EnsureBuiltins(t);
    std::vector<std::shared_ptr<const Entry>> copy;
    { std::lock_guard<std::mutex> lock(t.mutex); copy = t.entries; }
    for (auto& e : copy) e->write(go, out);
}

namespace detail {
/// Moves whenever a component serializer is registered: the records an object writes (so the order they load in) change with it.
std::uint64_t ComponentSerializerGeneration() { return g_serializerGeneration.load(std::memory_order_relaxed); }
} // namespace detail

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
