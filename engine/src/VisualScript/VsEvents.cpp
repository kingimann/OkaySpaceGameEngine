#include "okay/VisualScript/VsEvents.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <algorithm>

namespace okay::vs {

VsEventBindings& VsEventBindings::Get() {
    static VsEventBindings b;
    return b;
}

VsEventBindings::BindResult VsEventBindings::Bind(const Binding& b) {
    auto& list = m_byOwner[b.owner];
    std::size_t same = 0;
    for (const auto& e : list) {
        if (e.dispatcher != b.dispatcher) continue;
        ++same;
        if (e.listener == b.listener && e.handler == b.handler && e.isFunction == b.isFunction) return BindResult::AlreadyBound;
    }
    if (same >= kMaxPerDispatcher || CountListening(b.listener) >= kMaxPerListener) {
        if (list.empty()) m_byOwner.erase(b.owner);
        return BindResult::TooMany;
    }
    list.push_back(b);
    ++m_count;
    auto& owners = m_ownersOf[b.listener];
    if (std::find(owners.begin(), owners.end(), b.owner) == owners.end()) owners.push_back(b.owner);
    return BindResult::Added;
}

void VsEventBindings::DropOwnerRef(std::uint64_t listener, std::uint64_t owner) {
    auto it = m_byOwner.find(owner);
    if (it != m_byOwner.end())
        for (const auto& e : it->second) if (e.listener == listener) return;   // still bound on something
    auto ol = m_ownersOf.find(listener);
    if (ol == m_ownersOf.end()) return;
    ol->second.erase(std::remove(ol->second.begin(), ol->second.end(), owner), ol->second.end());
    if (ol->second.empty()) m_ownersOf.erase(ol);
}

bool VsEventBindings::Unbind(std::uint64_t owner, const std::string& dispatcher, std::uint64_t listener, const std::string& handler) {
    auto it = m_byOwner.find(owner);
    if (it == m_byOwner.end()) return false;
    auto& list = it->second;
    auto pos = std::find_if(list.begin(), list.end(), [&](const Binding& e) {
        return e.dispatcher == dispatcher && e.listener == listener && e.handler == handler; });
    if (pos == list.end()) return false;
    list.erase(pos);
    --m_count;
    if (list.empty()) m_byOwner.erase(it);
    DropOwnerRef(listener, owner);
    return true;
}

int VsEventBindings::UnbindAll(std::uint64_t owner, const std::string& dispatcher, std::uint64_t listener) {
    auto it = m_byOwner.find(owner);
    if (it == m_byOwner.end()) return 0;
    auto& list = it->second;
    const std::size_t before = list.size();
    list.erase(std::remove_if(list.begin(), list.end(), [&](const Binding& e) {
        return e.dispatcher == dispatcher && e.listener == listener; }), list.end());
    const int removed = (int)(before - list.size());
    m_count -= (std::size_t)removed;
    if (list.empty()) m_byOwner.erase(it);
    if (removed) DropOwnerRef(listener, owner);
    return removed;
}

std::vector<VsEventBindings::Binding> VsEventBindings::Snapshot(std::uint64_t owner, const std::string& dispatcher) const {
    std::vector<Binding> out;
    auto it = m_byOwner.find(owner);
    if (it == m_byOwner.end()) return out;
    for (const auto& e : it->second) if (e.dispatcher == dispatcher) out.push_back(e);
    return out;
}

void VsEventBindings::ForgetObject(std::uint64_t id) {
    // Where it listens.
    if (auto ol = m_ownersOf.find(id); ol != m_ownersOf.end()) {
        const std::vector<std::uint64_t> owners = ol->second;
        m_ownersOf.erase(ol);
        for (std::uint64_t o : owners) {
            auto it = m_byOwner.find(o);
            if (it == m_byOwner.end()) continue;
            auto& list = it->second;
            const std::size_t before = list.size();
            list.erase(std::remove_if(list.begin(), list.end(), [&](const Binding& e) { return e.listener == id; }), list.end());
            m_count -= before - list.size();
            if (list.empty()) m_byOwner.erase(it);
        }
    }
    // What it owns.
    if (auto it = m_byOwner.find(id); it != m_byOwner.end()) {
        const std::vector<Binding> list = std::move(it->second);
        m_byOwner.erase(it);
        m_count -= list.size();
        for (const auto& e : list) DropOwnerRef(e.listener, id);
    }
}

void VsEventBindings::ForgetScene(const Scene* scene) {
    if (m_count == 0) return;
    std::vector<std::uint64_t> ids;
    if (scene) for (const auto& up : scene->Objects()) if (up) ids.push_back(up->InstanceId());
    for (std::uint64_t id : ids) ForgetObject(id);
    // Anything whose object is already gone (destroyed without its component's destructor running yet).
    std::vector<std::uint64_t> dead;
    for (const auto& kv : m_byOwner) {
        if (!GameObject::FromInstanceId(kv.first)) dead.push_back(kv.first);
        for (const auto& e : kv.second) if (!GameObject::FromInstanceId(e.listener)) dead.push_back(e.listener);
    }
    for (std::uint64_t id : dead) ForgetObject(id);
}

void VsEventBindings::Clear() {
    m_byOwner.clear();
    m_ownersOf.clear();
    m_count = 0;
}

std::size_t VsEventBindings::CountFor(std::uint64_t owner, const std::string& dispatcher) const {
    auto it = m_byOwner.find(owner);
    if (it == m_byOwner.end()) return 0;
    std::size_t n = 0;
    for (const auto& e : it->second) if (e.dispatcher == dispatcher) ++n;
    return n;
}

std::size_t VsEventBindings::CountListening(std::uint64_t listener) const {
    auto ol = m_ownersOf.find(listener);
    if (ol == m_ownersOf.end()) return 0;
    std::size_t n = 0;
    for (std::uint64_t o : ol->second) {
        auto it = m_byOwner.find(o);
        if (it == m_byOwner.end()) continue;
        for (const auto& e : it->second) if (e.listener == listener) ++n;
    }
    return n;
}

} // namespace okay::vs
