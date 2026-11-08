#pragma once
#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
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

    /// Passed to Subscribe to get a Subscription handle back instead of a token.
    struct Scoped {};

    /// Move-only handle that unsubscribes when it goes out of scope. It must not
    /// outlive the bus it came from.
    class Subscription {
    public:
        Subscription() = default;
        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;
        Subscription(Subscription&& o) noexcept : m_bus(o.m_bus), m_token(o.m_token) { o.m_bus = nullptr; }
        Subscription& operator=(Subscription&& o) noexcept {
            if (this != &o) { Reset(); m_bus = o.m_bus; m_token = o.m_token; o.m_bus = nullptr; }
            return *this;
        }
        ~Subscription() { Reset(); }
        /// Unsubscribe now (safe to call twice).
        void Reset() { if (m_bus) { m_bus->Unsubscribe(m_token); m_bus = nullptr; } }
        bool Active() const { return m_bus != nullptr; }
        int Token() const { return m_token; }
    private:
        friend class EventBus;
        Subscription(EventBus* bus, int token) : m_bus(bus), m_token(token) {}
        EventBus* m_bus = nullptr;
        int m_token = 0;
    };

    /// Subscribe to events of type E. Returns a token used to Unsubscribe.
    template <typename E>
    int Subscribe(Handler<E> handler) {
        const std::type_index type(typeid(E));
        auto& list = m_handlers[type];
        int id = m_nextId++;
        auto entry = std::make_shared<Entry>();
        entry->id = id;
        entry->fn = [h = std::move(handler)](const void* e) { h(*static_cast<const E*>(e)); };
        list.push_back(std::move(entry));
        m_tokens.emplace(id, type);
        return id;
    }

    /// Subscribe and get an RAII handle that unsubscribes on destruction.
    template <typename E>
    Subscription Subscribe(Handler<E> handler, Scoped) {
        return Subscription(this, Subscribe<E>(std::move(handler)));
    }

    /// Remove a previously registered handler for event type E.
    template <typename E>
    void Unsubscribe(int token) {
        auto t = m_tokens.find(token);
        if (t == m_tokens.end() || t->second != std::type_index(typeid(E))) return;
        Unsubscribe(token);
    }

    /// Remove a handler by token alone (the bus remembers which event type it was for).
    /// Safe from inside a handler: the handler is skipped from then on, even by the
    /// Publish that is running.
    void Unsubscribe(int token) {
        auto t = m_tokens.find(token);
        if (t == m_tokens.end()) return;
        auto it = m_handlers.find(t->second);
        m_tokens.erase(t);
        if (it == m_handlers.end()) return;
        for (auto& e : it->second) {
            if (e->id != token) continue;
            e->alive = false;
            m_dirty = true;
            break;
        }
        if (m_depth == 0) Sweep();
    }

    /// Dispatch an event to all subscribers of its type. Handlers may Subscribe
    /// (they get the next event, not this one), Unsubscribe (a removed handler that
    /// has not run yet is skipped) and Publish again.
    template <typename E>
    void Publish(const E& event) {
        auto it = m_handlers.find(std::type_index(typeid(E)));
        if (it == m_handlers.end()) return;
        // Index loop up to the count at entry: a handler's Subscribe may grow (and
        // reallocate) the list, which neither invalidates the index nor runs the new one.
        auto& list = it->second;   // unordered_map values stay put when other types are added
        const std::size_t count = list.size();
        ++m_depth;
        for (std::size_t i = 0; i < count; ++i) {
            std::shared_ptr<Entry> entry = list[i];   // keeps the entry alive while its handler runs
            if (entry->alive) entry->fn(&event);
        }
        if (--m_depth == 0 && m_dirty) Sweep();
    }

    template <typename E>
    std::size_t SubscriberCount() const {
        auto it = m_handlers.find(std::type_index(typeid(E)));
        if (it == m_handlers.end()) return 0;
        std::size_t n = 0;
        for (auto& e : it->second) if (e->alive) ++n;
        return n;
    }

    /// Remove every subscriber (deferred to the end of the running Publish, if any).
    void Clear() {
        for (auto& kv : m_handlers) for (auto& e : kv.second) e->alive = false;
        m_tokens.clear();
        m_dirty = true;
        if (m_depth == 0) Sweep();
    }

private:
    struct Entry {
        int id = 0;
        bool alive = true;
        std::function<void(const void*)> fn;
    };

    // Erase dead entries; only called when no Publish is running.
    void Sweep() {
        for (auto& kv : m_handlers) {
            auto& list = kv.second;
            list.erase(std::remove_if(list.begin(), list.end(),
                                      [](const std::shared_ptr<Entry>& e) { return !e->alive; }),
                       list.end());
        }
        m_dirty = false;
    }

    std::unordered_map<std::type_index, std::vector<std::shared_ptr<Entry>>> m_handlers;
    std::unordered_map<int, std::type_index> m_tokens;
    int  m_nextId = 1;
    int  m_depth = 0;
    bool m_dirty = false;
};

} // namespace okay
