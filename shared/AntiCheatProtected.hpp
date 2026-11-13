#pragma once
// Anti-cheat: protected values (opt-in; docs/anticheat.md, "Protected values").
//
// okay::Protected<T> keeps a client-side value (health, ammo, a currency
// counter) in memory in a form a memory editor's "search for 100" can't find
// and a "freeze this address" can't hold:
//
//   * the value is stored XOR-ed with a keyed pad (the pad comes from a 64-bit
//     key); the key is replaced by a fresh one on EVERY write, so the stored
//     bytes of "100" differ from write to write and an old ciphertext copied back
//     decodes to something else;
//   * a keyed checksum (the canary) covers the plain value: editing the stored
//     words, or the key, makes it fail on the next read;
//   * a plain-text decoy copy sits beside it, as the lure: a scanner finds this
//     one, and when it's edited the decoy no longer matches the real value, which
//     is detected (and the real value is still returned).
//
// What this is NOT. It defeats naive memory editors and one-shot "set this
// address" tools. It does not stop a cheater who reads the code: the key lives
// next to the data, so anyone who finds and understands this class can decode
// or re-encode a value (and restoring a whole earlier snapshot of the object,
// key and canary together, is not detectable here). It is a speed bump on the
// client, never a substitute for the server owning health, ammo and money.
// Tamper reports are signals for the backend, not verdicts.
//
// Thread safety: every access takes a tiny per-instance spin lock, so Get / Set
// / Modify are safe from any thread (one writer at a time, readers see whole
// values). The object is not trivially cheap: expect tens of nanoseconds a call
// (tests/test_anticheat_protected.cpp prints the numbers).
//
// T must be trivially copyable and at most 64 bytes (int, float, bool, small
// structs).
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <thread>
#include <type_traits>

namespace okay {

enum class ProtectedTamper : std::uint8_t {
    None = 0,
    Decoy = 1,      // the plain copy was edited: the real value is intact and was returned
    Canary = 2,     // the stored words / key were edited: the value is unknown (T{} is returned until the next Set)
};

namespace protected_detail {

inline std::uint64_t Mix(std::uint64_t x) {   // splitmix64 finaliser
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

inline std::uint64_t FreshKey(const void* self) {
    static std::atomic<std::uint64_t> counter{0};
    static const std::uint64_t boot = (std::uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
    const std::uint64_t n = counter.fetch_add(0x2545F4914F6CDD1Dull, std::memory_order_relaxed);
    std::uint64_t k = Mix(boot ^ Mix(n) ^ (std::uint64_t)(std::uintptr_t)self);
    return k ? k : 0xA5A5A5A5A5A5A5A5ull;
}

// One process-wide tamper callback (set once at start-up: the game wires it to its
// anti-cheat report). Called outside the instance lock; keep it quick.
struct Hook {
    std::atomic<void (*)(ProtectedTamper, void*)> fn{nullptr};
    std::atomic<void*> user{nullptr};
    std::atomic<std::uint64_t> total{0};
};
inline Hook& GlobalHook() { static Hook h; return h; }

} // namespace protected_detail

// Called when any Protected<T> notices tampering (any thread). nullptr clears it.
inline void SetProtectedTamperHandler(void (*fn)(ProtectedTamper, void*), void* user = nullptr) {
    auto& h = protected_detail::GlobalHook();
    h.user.store(user, std::memory_order_relaxed);
    h.fn.store(fn, std::memory_order_release);
}
// Tamper events seen by every Protected<T> in the process.
inline std::uint64_t ProtectedTamperTotal() { return protected_detail::GlobalHook().total.load(std::memory_order_relaxed); }

template <class T>
class Protected {
    static_assert(std::is_trivially_copyable<T>::value, "Protected<T>: T must be trivially copyable");
    static_assert(sizeof(T) <= 64, "Protected<T>: T must be at most 64 bytes");
    static constexpr std::size_t kWords = (sizeof(T) + 7) / 8;

public:
    Protected() { Store(T{}); }
    Protected(T v) { Store(v); }   // implicit on purpose: `Protected<int> hp = 100;`
    Protected(const Protected& o) { Store(o.Get()); }
    Protected& operator=(const Protected& o) { if (this != &o) Set(o.Get()); return *this; }
    Protected& operator=(T v) { Set(v); return *this; }

    // The value. On tamper the handler is told and: a bad decoy still returns the
    // real value (and repairs the decoy); a bad canary returns T{}.
    T Get() const {
        T out{};
        const ProtectedTamper t = Read(out);
        if (t != ProtectedTamper::None) Notify(t);
        return out;
    }
    operator T() const { return Get(); }

    // Write: a new key, a new ciphertext, a new canary and decoy.
    void Set(T v) { Lock(); Store(v); Unlock(); }

    // Atomic read-modify-write: `hp.Modify([](int& h) { h -= 10; })`.
    template <class F> void Modify(F&& f) {
        Lock();
        T v{};
        const ProtectedTamper t = ReadLocked(v);
        f(v);
        Store(v);
        Unlock();
        if (t != ProtectedTamper::None) Notify(t);
    }

    template <class U = T, class = typename std::enable_if<std::is_arithmetic<U>::value>::type>
    Protected& operator+=(T d) { Modify([d](T& v) { v = (T)(v + d); }); return *this; }
    template <class U = T, class = typename std::enable_if<std::is_arithmetic<U>::value>::type>
    Protected& operator-=(T d) { Modify([d](T& v) { v = (T)(v - d); }); return *this; }

    // Check without returning the value. False if either copy was edited.
    bool Verify() const {
        T tmp{};
        const ProtectedTamper t = Read(tmp);
        if (t != ProtectedTamper::None) Notify(t);
        return t == ProtectedTamper::None;
    }
    // Tamper events this instance has seen.
    std::uint32_t TamperCount() const { return tampers_.load(std::memory_order_relaxed); }
    ProtectedTamper LastTamper() const { return (ProtectedTamper)last_.load(std::memory_order_relaxed); }

#ifdef OKAY_PROTECTED_TESTING
    // Test access to the raw storage, standing in for a memory editor.
    std::uint64_t* TestWords() { return enc_; }
    std::uint64_t* TestKey() { return &key_; }
    std::uint64_t* TestCanary() { return &canary_; }
    void TestSetDecoy(const T& v) { PutDecoy(v); }
    T TestGetDecoy() const { return GetDecoy(); }
#endif

private:
    void Lock() const {
        for (unsigned spins = 0; lock_.exchange(true, std::memory_order_acquire); ++spins)
            if (spins > 64) std::this_thread::yield();
    }
    void Unlock() const { lock_.store(false, std::memory_order_release); }

    static std::uint64_t Pad(std::uint64_t key, std::size_t i) { return protected_detail::Mix(key + 0x1000193ull * (i + 1)); }
    static std::uint64_t Canary(std::uint64_t key, const std::uint64_t* plain) {
        std::uint64_t h = protected_detail::Mix(key ^ 0xC0FFEE1234567ull);
        for (std::size_t i = 0; i < kWords; ++i) h = protected_detail::Mix(h ^ plain[i]);
        return h;
    }

    // The decoy is volatile bytes so the compiler can't drop the stores as dead.
    void PutDecoy(const T& v) const {
        unsigned char b[sizeof(T)];
        std::memcpy(b, &v, sizeof(T));
        for (std::size_t i = 0; i < sizeof(T); ++i) decoy_[i] = b[i];
    }
    T GetDecoy() const {
        unsigned char b[sizeof(T)];
        for (std::size_t i = 0; i < sizeof(T); ++i) b[i] = decoy_[i];
        T v;
        std::memcpy(&v, b, sizeof(T));
        return v;
    }

    void Store(const T& v) {   // caller holds the lock (or is a constructor)
        std::uint64_t plain[kWords] = {};
        std::memcpy(plain, &v, sizeof(T));
        std::uint64_t k = protected_detail::FreshKey(this);
        if (k == key_) k = protected_detail::Mix(k);   // never reuse the previous key
        for (std::size_t i = 0; i < kWords; ++i) enc_[i] = plain[i] ^ Pad(k, i);
        canary_ = Canary(k, plain);
        key_ = k;
        PutDecoy(v);
    }

    ProtectedTamper ReadLocked(T& out) const {
        std::uint64_t plain[kWords];
        for (std::size_t i = 0; i < kWords; ++i) plain[i] = enc_[i] ^ Pad(key_, i);
        if (Canary(key_, plain) != canary_) { out = T{}; return ProtectedTamper::Canary; }
        std::memcpy(&out, plain, sizeof(T));
        const T d = GetDecoy();   // the plain lure
        if (std::memcmp(&d, &out, sizeof(T)) != 0) {
            PutDecoy(out);   // repair the lure so one edit is one event
            return ProtectedTamper::Decoy;
        }
        return ProtectedTamper::None;
    }
    ProtectedTamper Read(T& out) const {
        Lock();
        const ProtectedTamper t = ReadLocked(out);
        Unlock();
        return t;
    }

    void Notify(ProtectedTamper t) const {
        tampers_.fetch_add(1, std::memory_order_relaxed);
        last_.store((std::uint8_t)t, std::memory_order_relaxed);
        auto& h = protected_detail::GlobalHook();
        h.total.fetch_add(1, std::memory_order_relaxed);
        if (auto fn = h.fn.load(std::memory_order_acquire)) fn(t, h.user.load(std::memory_order_relaxed));
    }

    std::uint64_t enc_[kWords];
    std::uint64_t key_ = 0;
    std::uint64_t canary_ = 0;
    mutable volatile unsigned char decoy_[sizeof(T)];
    mutable std::atomic<bool> lock_{false};
    mutable std::atomic<std::uint32_t> tampers_{0};
    mutable std::atomic<std::uint8_t> last_{0};
};

} // namespace okay
