#pragma once
#include <algorithm>
#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace okay {

class EventBus;

/// RAII handle for a subscription made with EventBus::Listen: unsubscribes when it
/// goes out of scope. Move-only. Safe to outlive the bus (it then does nothing).
class Subscription {
public:
    Subscription() = default;
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    Subscription(Subscription&& o) noexcept { *this = std::move(o); }
    Subscription& operator=(Subscription&& o) noexcept {
        if (this != &o) { Unsubscribe(); m_remove = std::move(o.m_remove); m_alive = std::move(o.m_alive); o.m_remove = nullptr; }
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
    /// Give up ownership: the handler stays subscribed after this object is gone.
    void Release() { m_remove = nullptr; }

private:
    friend class EventBus;
    std::function<void()> m_remove;
    std::weak_ptr<int> m_alive;
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
///  - Listen: like Subscribe but returns a Subscription that unsubscribes itself.
///  - Queue / Flush: Queue stores a copy of an event; Flush (call it at a point
///    you choose, e.g. start of a frame) publishes everything queued so far.
///    Events queued while flushing wait for the next Flush.
///
/// Publish dispatches to a snapshot of the handlers taken when it starts, so a
/// handler may subscribe or unsubscribe during dispatch: new handlers first see
/// the next event, and one that was just removed still receives the current one.
/// Not thread safe: use it from one thread (or guard it yourself).
class EventBus {
public:
    template <typename E>
    using Handler = std::function<void(const E&)>;

    EventBus() : m_alive(std::make_shared<int>(0)) {}
    // A copy gets its own handlers but a fresh identity, so Subscriptions made on the
    // original never act on the copy.
    EventBus(const EventBus& o)
        : m_handlers(o.m_handlers), m_nextId(o.m_nextId), m_queue(o.m_queue), m_alive(std::make_shared<int>(0)) {}
    EventBus& operator=(const EventBus& o) {
        if (this != &o) { m_handlers = o.m_handlers; m_nextId = o.m_nextId; m_queue = o.m_queue; m_alive = std::make_shared<int>(0); }
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

    /// Subscribe and get a scoped handle back: the handler is removed when the
    /// Subscription is destroyed (or Unsubscribe()d, or Release()d to keep it).
    template <typename E>
    Subscription Listen(Handler<E> handler, int priority = 0, bool once = false) {
        const int id = Add<E>(std::move(handler), priority, once);
        Subscription s;
        s.m_alive = m_alive;
        s.m_remove = [this, id]() { Unsubscribe<E>(id); };
        return s;
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
        for (auto& entry : snapshot) {
            if (entry.fired) {
                if (*entry.fired) continue;       // a once-only handler runs one time, even if re-published into
                *entry.fired = true;
                Unsubscribe<E>(entry.id);
            }
            entry.fn(&event);
        }
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
        return it == m_handlers.end() ? 0 : it->second.size();
    }

    void Clear() { m_handlers.clear(); }

private:
    struct Entry {
        int id;
        int priority = 0;
        std::shared_ptr<bool> fired;   // set for once-only handlers
        std::function<void(const void*)> fn;
    };

    template <typename E>
    int Add(Handler<E> handler, int priority, bool once) {
        auto& list = m_handlers[std::type_index(typeid(E))];
        const int id = m_nextId++;
        Entry e;
        e.id = id; e.priority = priority;
        if (once) e.fired = std::make_shared<bool>(false);
        e.fn = [h = std::move(handler)](const void* ev) { h(*static_cast<const E*>(ev)); };
        // Keep the list ordered: higher priority first, then subscription order.
        auto pos = std::find_if(list.begin(), list.end(),
                                [priority](const Entry& x) { return x.priority < priority; });
        list.insert(pos, std::move(e));
        return id;
    }

    std::unordered_map<std::type_index, std::vector<Entry>> m_handlers;
    int m_nextId = 1;
    std::vector<std::function<void(EventBus&)>> m_queue;
    std::shared_ptr<int> m_alive;
};

} // namespace okay
