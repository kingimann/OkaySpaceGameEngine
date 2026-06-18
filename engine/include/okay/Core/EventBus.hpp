#pragma once
#include <algorithm>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace okay {

/// A lightweight, type-safe publish/subscribe event bus. Any struct/class can
/// be an event; subscribers register a callback for a specific event type and
/// receive every published instance of it.
///
///     struct PlayerDied { int score; };
///     bus.Subscribe<PlayerDied>([](const PlayerDied& e){ ... });
///     bus.Publish(PlayerDied{1234});
class EventBus {
public:
    template <typename E>
    using Handler = std::function<void(const E&)>;

    /// Subscribe to events of type E. Returns a token used to Unsubscribe.
    template <typename E>
    int Subscribe(Handler<E> handler) {
        auto& list = m_handlers[std::type_index(typeid(E))];
        int id = m_nextId++;
        list.push_back({id, [h = std::move(handler)](const void* e) {
            h(*static_cast<const E*>(e));
        }});
        return id;
    }

    /// Remove a previously registered handler for event type E.
    template <typename E>
    void Unsubscribe(int token) {
        auto it = m_handlers.find(std::type_index(typeid(E)));
        if (it == m_handlers.end()) return;
        auto& list = it->second;
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [token](const Entry& e) { return e.id == token; }),
                   list.end());
    }

    /// Dispatch an event to all subscribers of its type.
    template <typename E>
    void Publish(const E& event) {
        auto it = m_handlers.find(std::type_index(typeid(E)));
        if (it == m_handlers.end()) return;
        // Copy so handlers may (un)subscribe during dispatch.
        auto snapshot = it->second;
        for (auto& entry : snapshot) entry.fn(&event);
    }

    template <typename E>
    std::size_t SubscriberCount() const {
        auto it = m_handlers.find(std::type_index(typeid(E)));
        return it == m_handlers.end() ? 0 : it->second.size();
    }

    void Clear() { m_handlers.clear(); }

private:
    struct Entry {
        int id;
        std::function<void(const void*)> fn;
    };
    std::unordered_map<std::type_index, std::vector<Entry>> m_handlers;
    int m_nextId = 1;
};

} // namespace okay
