#pragma once
// ---------------------------------------------------------------------------
// Event dispatchers: which object listens to which object's dispatcher.
//
// A graph declares `dispatcher Died(Object who)`. Another graph (or the same one)
// Binds one of its handlers - a custom event or a function - to it, and Call
// Dispatcher runs every bound handler in the order they were bound. The table holds
// object instance ids only, never pointers: a destroyed object is simply not found
// when a call looks it up, and the table is also cleaned eagerly when an object goes
// (VisualScriptComponent's destructor) or a scene is cleared / destroyed.
//
// Single-threaded, like the rest of visual scripting (no locks here).
// ---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay { class Scene; }

namespace okay::vs {

class VsEventBindings {
public:
    /// Most listeners one dispatcher may have, and most bindings one listener may hold.
    static constexpr std::size_t kMaxPerDispatcher = 256;
    static constexpr std::size_t kMaxPerListener = 1024;

    struct Binding {
        std::uint64_t owner = 0;      ///< instance id of the object that owns the dispatcher
        std::uint64_t listener = 0;   ///< instance id of the object whose handler runs
        std::string dispatcher;
        bool isFunction = false;      ///< the handler is a function of the listener's graph (else a custom event)
        std::string handler;
    };
    enum class BindResult { Added, AlreadyBound, TooMany };

    static VsEventBindings& Get();

    /// Add a binding. The same (owner, dispatcher, listener, handler) twice is one binding.
    BindResult Bind(const Binding& b);
    /// Remove one binding. True when it existed.
    bool Unbind(std::uint64_t owner, const std::string& dispatcher, std::uint64_t listener, const std::string& handler);
    /// Remove every binding `listener` has on `owner`'s `dispatcher` (any handler). Returns how many.
    int UnbindAll(std::uint64_t owner, const std::string& dispatcher, std::uint64_t listener);
    /// The bindings of `owner`'s `dispatcher`, in the order they were bound (a copy: a handler may
    /// bind or unbind while the call runs).
    std::vector<Binding> Snapshot(std::uint64_t owner, const std::string& dispatcher) const;

    /// An object is going away: drop every binding it owns and every binding where it listens.
    void ForgetObject(std::uint64_t id);
    /// A scene is cleared or destroyed: drop the bindings of its objects (and any whose
    /// object no longer exists).
    void ForgetScene(const Scene* scene);
    /// Drop everything (tests, a new run).
    void Clear();

    std::size_t Count() const { return m_count; }
    std::size_t CountFor(std::uint64_t owner, const std::string& dispatcher) const;
    std::size_t CountListening(std::uint64_t listener) const;

private:
    std::unordered_map<std::uint64_t, std::vector<Binding>> m_byOwner;
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> m_ownersOf;   // listener -> owners it is bound on
    std::size_t m_count = 0;
    void DropOwnerRef(std::uint64_t listener, std::uint64_t owner);
};

} // namespace okay::vs
