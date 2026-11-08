#pragma once
#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace okay {

class EventBus;

/// RAII handle for a subscription made with EventBus::Listen (or Subscribe with
/// EventBus::Scoped): unsubscribes when it goes out of scope. Move-only. Safe to
/// outlive the bus (it then does nothing).
class Subscription {
public:
    Subscription() = default;
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    Subscription(Subscription&& o) noexcept { *this = std::move(o); }
    Subscription& operator=(Subscription&& o) noexcept {
        if (this != &o) {
            Unsubscribe();
            m_remove = std::move(o.m_remove); m_alive = std::move(o.m_alive); m_token = o.m_token;
            o.m_remove = nullptr;
        }
        return *this;
    }
    ~Subscription() { Unsubscribe(); }

    /// True while the handler is still subscribed.
    bool Active() const { return m_remove && !m_alive.expired(); }
    /// Remove the handler now (also done by the destructor). Safe to call twice.
    void Unsubscribe() {
        if (m_remove && !m_alive.expired()) m_remove();
        m_remove = nullptr;
    }
    void Reset() { Unsubscribe(); }
    /// Give up ownership: the handler stays subscribed after this object is gone.
    void Release() { m_remove = nullptr; }
    int Token() const { return m_token; }

private:
    friend class EventBus;
    std::function<void()> m_remove;
    std::weak_ptr<int> m_alive;
    int m_token = 0;
};

/// A lightweight, type-safe publish/subscribe event bus. Any struct/class can
/// be an event; subscribers register a callback for a specific event type and
/// receive every published instance of it.
///
///     struct PlayerDied { int score; };
///     bus.Subscribe<PlayerDied>([](const PlayerDied& e){ ... });
///     bus.Publish(PlayerDied{1234});
///
/// Extras:
///  - priority: handlers with a higher priority run first (same priority: in the
///    order they subscribed). The default is 0.
///  - SubscribeOnce: the handler runs for the next event only.
///  - Listen / Subscribe(h, Scoped{}): return a Subscription that unsubscribes itself.
///  - Queue / Flush: Queue stores a copy of an event; Flush (call it at a point
///    you choose, e.g. start of a frame) publishes everything queued so far.
///    Events queued while flushing wait for the next Flush.
///
/// Publish dispatches over the handlers present when it starts: a handler may
/// Subscribe (the new one first sees the next event), Unsubscribe (a removed handler
/// that has not run yet is skipped) and Publish again.
/// Not thread safe: use it from one thread (or guard it yourself).
class EventBus {
public:
    template <typename E>
    using Handler = std::function<void(const E&)>;

    /// Passed to Subscribe to get a Subscription handle back instead of a token.
    struct Scoped {};
    using Subscription = ::okay::Subscription;

    EventBus() : m_alive(std::make_shared<int>(0)) {}
    // A copy gets its own handlers but a fresh identity, so Subscriptions made on the
    // original never act on the copy.
    EventBus(const EventBus& o) : m_alive(std::make_shared<int>(0)) { CopyFrom(o); }
    EventBus& operator=(const EventBus& o) {
        if (this != &o) { CopyFrom(o); m_alive = std::make_shared<int>(0); }
        return *this;
    }

    /// Subscribe to events of type E. Returns a token used to Unsubscribe.
    template <typename E>
    int Subscribe(Handler<E> handler, int priority = 0) {
        return Add<E>(std::move(handler), priority, false);
    }

    /// Subscribe for the next event of type E only.
    template <typename E>
    int SubscribeOnce(Handler<E> handler, int priority = 0) {
        return Add<E>(std::move(handler), priority, true);
    }

    /// Subscribe and get an RAII handle that unsubscribes on destruction.
    template <typename E>
    Subscription Subscribe(Handler<E> handler, Scoped, int priority = 0) {
        return Listen<E>(std::move(handler), priority, false);
    }

    /// Subscribe and get a scoped handle back: the handler is removed when the
    /// Subscription is destroyed (or Unsubscribe()d, or Release()d to keep it).
    template <typename E>
    Subscription Listen(Handler<E> handler, int priority = 0, bool once = false) {
        const int id = Add<E>(std::move(handler), priority, once);
        Subscription s;
        s.m_alive = m_alive;
        s.m_token = id;
        s.m_remove = [this, id]() { Unsubscribe(id); };
        return s;
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

    /// Dispatch an event to all subscribers of its type.
    template <typename E>
    void Publish(const E& event) {
        auto it = m_handlers.find(std::type_index(typeid(E)));
        if (it == m_handlers.end()) return;
        // Snapshot: handlers may (un)subscribe, which can insert in the middle of the list.
        const std::vector<std::shared_ptr<Entry>> snapshot = it->second;
        ++m_depth;
        for (const auto& entry : snapshot) {
            if (!entry->alive) continue;
            if (entry->once) {
                if (entry->fired) continue;       // a once-only handler runs one time, even if re-published into
                entry->fired = true;
                Unsubscribe(entry->id);
            }
            entry->fn(&event);
        }
        if (--m_depth == 0 && m_dirty) Sweep();
    }

    /// Store a copy of the event to be published at the next Flush().
    template <typename E>
    void Queue(const E& event) {
        m_queue.push_back([event](EventBus& bus) { bus.Publish(event); });
    }

    /// Publish every queued event, in the order queued; returns how many. Events queued by
    /// handlers during the flush are kept for the next one.
    std::size_t Flush() {
        std::vector<std::function<void(EventBus&)>> batch;
        batch.swap(m_queue);
        for (auto& fn : batch) fn(*this);
        return batch.size();
    }
    std::size_t QueuedCount() const { return m_queue.size(); }
    void ClearQueue() { m_queue.clear(); }

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
        int priority = 0;
        bool alive = true;
        bool once = false;
        bool fired = false;
        std::function<void(const void*)> fn;
    };

    template <typename E>
    int Add(Handler<E> handler, int priority, bool once) {
        const std::type_index type(typeid(E));
        auto& list = m_handlers[type];
        const int id = m_nextId++;
        auto entry = std::make_shared<Entry>();
        entry->id = id; entry->priority = priority; entry->once = once;
        entry->fn = [h = std::move(handler)](const void* ev) { h(*static_cast<const E*>(ev)); };
        // Keep the list ordered: higher priority first, then subscription order.
        auto pos = std::find_if(list.begin(), list.end(),
                                [priority](const std::shared_ptr<Entry>& x) { return x->priority < priority; });
        list.insert(pos, std::move(entry));
        m_tokens.emplace(id, type);
        return id;
    }

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

    void CopyFrom(const EventBus& o) {
        m_handlers.clear();
        for (auto& kv : o.m_handlers) {
            auto& list = m_handlers[kv.first];
            for (auto& e : kv.second) if (e->alive) list.push_back(std::make_shared<Entry>(*e));
        }
        m_tokens = o.m_tokens;
        m_nextId = o.m_nextId;
        m_queue = o.m_queue;
        m_depth = 0;
        m_dirty = false;
    }

    std::unordered_map<std::type_index, std::vector<std::shared_ptr<Entry>>> m_handlers;
    std::unordered_map<int, std::type_index> m_tokens;
    int  m_nextId = 1;
    int  m_depth = 0;
    bool m_dirty = false;
    std::vector<std::function<void(EventBus&)>> m_queue;
    std::shared_ptr<int> m_alive;
};

} // namespace okay
