#pragma once
// Small allocators for the runtime: a per-frame linear arena and a typed object
// pool, with a shared stats struct. Header-only and header-light. None of it
// throws on failure (a refused allocation returns nullptr and is counted) and
// none of it is thread safe: give each thread its own. See docs/core_runtime.md.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace okay {

/// Counters shared by FrameArena and PoolAllocator.
struct AllocStats {
    std::size_t used = 0;            // arena: bytes handed out since the last Reset; pool: bytes of live objects
    std::size_t reserved = 0;        // bytes held from the system
    std::size_t limit = 0;           // 0 = unlimited
    std::size_t highWater = 0;       // the largest `used` ever seen
    std::size_t live = 0;            // arena: allocations since the last Reset; pool: live objects
    std::uint64_t allocations = 0;   // successful allocations, ever
    std::uint64_t failures = 0;      // refused: over the limit, a bad alignment, or out of memory
    std::uint64_t resets = 0;        // arena Reset() calls
};

#ifdef NDEBUG
inline constexpr bool kPoisonByDefault = false;
#else
inline constexpr bool kPoisonByDefault = true;
#endif
/// What freed or reset memory is filled with when poisoning is on.
inline constexpr unsigned char kPoisonByte = 0xDD;

/// A linear ("bump") allocator for per-frame scratch memory. Allocate as much as you
/// like during the frame, call Reset() at the start of the next one, and everything is
/// gone at once: no per-object frees. Nothing handed out is ever destructed, so keep
/// to trivially destructible data (or destroy it yourself before the Reset).
///
/// It grows by chunks, so a pointer stays valid until Reset (or a Rewind past it). A
/// Reset after a frame that needed several chunks replaces them with one chunk big
/// enough for that frame, so a steady workload stops allocating from the system.
/// `limit` caps the bytes handed out between resets (0 = no cap): going over gives
/// nullptr, never an exception.
class FrameArena {
public:
    /// A position to Rewind to, to give back part of a frame.
    struct Marker {
        std::size_t used = 0;
    private:
        friend class FrameArena;
        void* chunk = nullptr;
        std::size_t offset = 0;
        std::uint64_t epoch = 0;
    };

    explicit FrameArena(std::size_t initialCapacity = 64 * 1024, std::size_t limit = 0)
        : m_initial(initialCapacity), m_limit(limit) {}
    ~FrameArena() { FreeAll(); }
    FrameArena(const FrameArena&) = delete;
    FrameArena& operator=(const FrameArena&) = delete;
    FrameArena(FrameArena&& o) noexcept { Steal(o); }
    FrameArena& operator=(FrameArena&& o) noexcept {
        if (this != &o) { FreeAll(); Steal(o); }
        return *this;
    }

    /// Fill reset memory with kPoisonByte so a pointer used after Reset shows up as
    /// garbage instead of plausible old data. On by default in debug builds.
    bool poisonOnReset = kPoisonByDefault;

    /// `bytes` of memory aligned to `alignment` (a power of two), or nullptr when it
    /// would pass the limit, the alignment is not a power of two, or memory ran out.
    /// The memory is uninitialised. A request for 0 bytes gets 1.
    void* Allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t)) noexcept {
        if (bytes == 0) bytes = 1;
        if (alignment == 0 || (alignment & (alignment - 1)) != 0) { ++m_failures; return nullptr; }
        if (m_cur) {
            if (void* p = TakeFrom(m_cur, bytes, alignment)) return p;
            if (m_limitHit) { m_limitHit = false; ++m_failures; return nullptr; }
        }
        return AllocateSlow(bytes, alignment);
    }
    /// Like Allocate, filled with zeros.
    void* AllocateZeroed(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t)) noexcept {
        void* p = Allocate(bytes, alignment);
        if (p) std::memset(p, 0, bytes ? bytes : 1);
        return p;
    }

    /// Room for `count` Ts (uninitialised), or nullptr.
    template <class T>
    T* AllocArray(std::size_t count) noexcept {
        static_assert(std::is_trivially_destructible_v<T>, "FrameArena never runs destructors");
        if (count > static_cast<std::size_t>(-1) / sizeof(T)) { ++m_failures; return nullptr; }
        return static_cast<T*>(Allocate(count * sizeof(T), alignof(T)));
    }
    /// Construct a T in the arena, or nullptr when there is no room.
    template <class T, class... Args>
    T* Make(Args&&... args) {
        static_assert(std::is_trivially_destructible_v<T>, "FrameArena never runs destructors");
        void* p = Allocate(sizeof(T), alignof(T));
        return p ? ::new (p) T(std::forward<Args>(args)...) : nullptr;
    }
    /// A NUL-terminated copy of `s` in the arena, or nullptr.
    const char* CopyString(std::string_view s) noexcept {
        char* p = static_cast<char*>(Allocate(s.size() + 1, 1));
        if (!p) return nullptr;
        if (!s.empty()) std::memcpy(p, s.data(), s.size());
        p[s.size()] = '\0';
        return p;
    }

    /// Where the arena is now.
    Marker Mark() const {
        Marker m;
        m.used = m_used;
        m.chunk = m_cur;
        m.offset = m_cur ? static_cast<Chunk*>(m_cur)->used : 0;
        m.epoch = m_resets;
        return m;
    }
    /// Give back everything allocated since `m` was taken. False (and nothing happens)
    /// if a Reset came in between.
    bool Rewind(const Marker& m) {
        if (m.epoch != m_resets || m.used > m_used) return false;
        Chunk* c = static_cast<Chunk*>(m.chunk);
        Chunk* from = c ? c : m_head;
        if (!from) return true;
        if (c) {
            if (m.offset > c->used) return false;   // markers rewound out of order
            Poison(c->Data() + m.offset, c->used - m.offset);
            c->used = m.offset;
            from = c->next;
        }
        for (Chunk* it = from; it; it = it->next) { Poison(it->Data(), it->used); it->used = 0; }
        m_cur = c ? c : m_head;
        m_used = m.used;
        return true;
    }

    /// Start a new frame: every pointer handed out so far is dead.
    void Reset() {
        const std::size_t frameUsed = m_used;
        if (poisonOnReset) for (Chunk* c = m_head; c; c = c->next) Poison(c->Data(), c->used);
        if (m_head && m_head->next) {
            // The frame outgrew the first chunk: swap the chain for one chunk that fits it.
            FreeChunks();
            const std::size_t want = frameUsed + frameUsed / 8 + 64;
            Chunk* c = NewChunk(want > m_initial ? want : m_initial);
            if (c) { m_head = m_tail = m_cur = c; m_reserved = c->size; }
        } else if (m_head) {
            m_head->used = 0;
            m_cur = m_head;
        }
        m_used = 0;
        m_frameAllocs = 0;
        ++m_resets;
    }

    std::size_t Used() const { return m_used; }
    std::size_t HighWater() const { return m_high; }
    std::size_t Reserved() const { return m_reserved; }
    std::size_t Limit() const { return m_limit; }
    void SetLimit(std::size_t bytes) { m_limit = bytes; }
    AllocStats Stats() const {
        AllocStats s;
        s.used = m_used; s.reserved = m_reserved; s.limit = m_limit; s.highWater = m_high;
        s.live = m_frameAllocs; s.allocations = m_allocs; s.failures = m_failures; s.resets = m_resets;
        return s;
    }

private:
    struct alignas(std::max_align_t) Chunk {
        Chunk* next;
        std::size_t size;   // bytes of data
        std::size_t used;
        char* Data() { return reinterpret_cast<char*>(this + 1); }
    };

    void Poison(char* p, std::size_t n) const {
        if (poisonOnReset && n) std::memset(p, kPoisonByte, n);
    }

    // Carve `bytes` out of `c`, or nullptr (m_limitHit says whether the limit is why).
    void* TakeFrom(void* chunk, std::size_t bytes, std::size_t align) noexcept {
        Chunk* c = static_cast<Chunk*>(chunk);
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(c->Data()) + c->used;
        const std::uintptr_t aligned = (base + (align - 1)) & ~static_cast<std::uintptr_t>(align - 1);
        const std::size_t pad = static_cast<std::size_t>(aligned - base);
        if (pad > c->size - c->used || bytes > c->size - c->used - pad) return nullptr;
        const std::size_t take = pad + bytes;
        if (m_limit && (take > m_limit || m_used > m_limit - take)) { m_limitHit = true; return nullptr; }
        c->used += take;
        m_used += take;
        if (m_used > m_high) m_high = m_used;
        ++m_allocs;
        ++m_frameAllocs;
        return reinterpret_cast<void*>(aligned);
    }

    void* AllocateSlow(std::size_t bytes, std::size_t align) noexcept {
        // A chunk kept from an earlier frame or Rewind may fit.
        while (m_cur && m_cur->next) {
            m_cur = m_cur->next;
            if (void* p = TakeFrom(m_cur, bytes, align)) return p;
            if (m_limitHit) { m_limitHit = false; ++m_failures; return nullptr; }
        }
        if (bytes > static_cast<std::size_t>(-1) - align - sizeof(Chunk)) { ++m_failures; return nullptr; }
        if (m_limit && bytes > m_limit - (m_used < m_limit ? m_used : m_limit)) { ++m_failures; return nullptr; }
        std::size_t size = m_initial;
        if (m_tail && m_tail->size > size) size = m_tail->size;
        if (m_tail) size = size > static_cast<std::size_t>(-1) / 2 ? size : size * 2;
        if (m_limit) {
            const std::size_t room = m_limit - m_used;
            const std::size_t cap = room + align;
            if (size > cap) size = cap;
        }
        if (size < bytes + align) size = bytes + align;
        Chunk* c = NewChunk(size);
        if (!c) { ++m_failures; return nullptr; }
        if (m_tail) m_tail->next = c; else m_head = c;
        m_tail = c;
        m_cur = c;
        m_reserved += c->size;
        if (void* p = TakeFrom(c, bytes, align)) return p;
        m_limitHit = false;
        ++m_failures;
        return nullptr;
    }

    static Chunk* NewChunk(std::size_t dataBytes) noexcept {
        if (dataBytes > static_cast<std::size_t>(-1) - sizeof(Chunk)) return nullptr;
        void* mem = std::malloc(sizeof(Chunk) + dataBytes);
        if (!mem) return nullptr;
        Chunk* c = static_cast<Chunk*>(mem);
        c->next = nullptr; c->size = dataBytes; c->used = 0;
        return c;
    }

    void FreeChunks() noexcept {
        for (Chunk* c = m_head; c;) { Chunk* n = c->next; std::free(c); c = n; }
        m_head = m_tail = nullptr;
        m_cur = nullptr;
        m_reserved = 0;
    }
    void FreeAll() noexcept { FreeChunks(); }

    void Steal(FrameArena& o) noexcept {
        poisonOnReset = o.poisonOnReset;
        m_initial = o.m_initial; m_limit = o.m_limit;
        m_head = o.m_head; m_tail = o.m_tail; m_cur = o.m_cur;
        m_used = o.m_used; m_high = o.m_high; m_reserved = o.m_reserved;
        m_allocs = o.m_allocs; m_failures = o.m_failures; m_resets = o.m_resets; m_frameAllocs = o.m_frameAllocs;
        o.m_head = o.m_tail = o.m_cur = nullptr;
        o.m_used = o.m_reserved = 0;
    }

    std::size_t m_initial = 0, m_limit = 0;
    Chunk* m_head = nullptr;
    Chunk* m_tail = nullptr;
    Chunk* m_cur = nullptr;
    std::size_t m_used = 0, m_high = 0, m_reserved = 0, m_frameAllocs = 0;
    std::uint64_t m_allocs = 0, m_failures = 0, m_resets = 0;
    bool m_limitHit = false;
};

/// Rewinds an arena to where it was when the scope began.
class ArenaScope {
public:
    explicit ArenaScope(FrameArena& a) : m_arena(a), m_mark(a.Mark()) {}
    ~ArenaScope() { m_arena.Rewind(m_mark); }
    ArenaScope(const ArenaScope&) = delete;
    ArenaScope& operator=(const ArenaScope&) = delete;
private:
    FrameArena& m_arena;
    FrameArena::Marker m_mark;
};

/// A standard-library allocator over a FrameArena: `std::vector<int, ArenaAllocator<int>> v{&arena};`
/// deallocate does nothing, and the container must be gone (or not used) by the next Reset.
/// Running out of room throws std::bad_alloc, as standard containers expect.
template <class T>
struct ArenaAllocator {
    using value_type = T;
    FrameArena* arena = nullptr;
    ArenaAllocator() = default;
    explicit ArenaAllocator(FrameArena* a) : arena(a) {}
    template <class U> ArenaAllocator(const ArenaAllocator<U>& o) noexcept : arena(o.arena) {}
    T* allocate(std::size_t n) {
        if (!arena || n > static_cast<std::size_t>(-1) / sizeof(T)) throw std::bad_alloc();
        void* p = arena->Allocate(n * sizeof(T), alignof(T));
        if (!p) throw std::bad_alloc();
        return static_cast<T*>(p);
    }
    void deallocate(T*, std::size_t) noexcept {}
    template <class U> bool operator==(const ArenaAllocator<U>& o) const noexcept { return arena == o.arena; }
    template <class U> bool operator!=(const ArenaAllocator<U>& o) const noexcept { return arena != o.arena; }
};

/// A pool of T with stable addresses and a free list: Create() hands out a slot
/// (reusing the most recently freed one first), Destroy() gives it back. Memory grows
/// in blocks and is never moved, so a pointer stays valid until its own Destroy.
/// `maxObjects` caps the live count (0 = no cap); a refused Create returns nullptr.
/// Objects still alive when the pool is destroyed (or Clear()ed) are destructed.
template <class T>
class PoolAllocator {
public:
    explicit PoolAllocator(std::size_t blockSize = 64, std::size_t maxObjects = 0)
        : m_blockSize(blockSize ? blockSize : 1), m_limit(maxObjects) {}
    ~PoolAllocator() { Release(); }
    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    /// Fill a freed slot with kPoisonByte. On by default in debug builds.
    bool poisonOnFree = kPoisonByDefault;

    /// Construct a T in a free slot. nullptr when the pool is full (`maxObjects`) or out of
    /// memory. If T's constructor throws, the slot is given back and the exception propagates.
    template <class... Args>
    T* Create(Args&&... args) {
        Slot* s = Acquire();
        if (!s) { ++m_failures; return nullptr; }
        T* obj;
        try {
            obj = ::new (static_cast<void*>(s->data)) T(std::forward<Args>(args)...);
        } catch (...) {
            s->next = m_free; m_free = s;
            throw;
        }
        s->live = true;
        ++m_live;
        ++m_allocs;
        if (m_live > m_high) m_high = m_live;
        return obj;
    }

    /// Destruct `p` and free its slot. False for nullptr and for a slot that is not live
    /// (a double Destroy). `p` must have come from this pool's Create.
    bool Destroy(T* p) noexcept {
        if (!p) return false;
        Slot* s = SlotOf(p);
        if (!s->live) return false;
        p->~T();
        s->live = false;
        if (poisonOnFree) std::memset(s->data, kPoisonByte, sizeof(T));
        s->next = m_free;
        m_free = s;
        --m_live;
        return true;
    }

    /// True if `p` points at a slot of this pool (live or free). Walks the blocks.
    bool Owns(const T* p) const {
        const unsigned char* q = reinterpret_cast<const unsigned char*>(p);
        for (const Block& b : m_blocks) {
            const unsigned char* lo = reinterpret_cast<const unsigned char*>(b.slots.get());
            if (q >= lo && q < lo + b.count * sizeof(Slot) &&
                static_cast<std::size_t>(q - lo) % sizeof(Slot) == 0) return true;
        }
        return false;
    }
    bool IsLive(const T* p) const { return p && Owns(p) && SlotOf(const_cast<T*>(p))->live; }

    /// Call `fn(T&)` for every live object (order is by memory, not by creation).
    template <class F>
    void ForEach(F&& fn) {
        for (Block& b : m_blocks)
            for (std::size_t i = 0; i < b.count; ++i)
                if (b.slots[i].live) fn(*std::launder(reinterpret_cast<T*>(b.slots[i].data)));
    }

    /// Destruct every live object and make every slot free again; keeps the memory.
    void Clear() {
        m_free = nullptr;
        for (Block& b : m_blocks) {
            for (std::size_t i = b.count; i-- > 0;) {
                Slot& s = b.slots[i];
                if (s.live) {
                    std::launder(reinterpret_cast<T*>(s.data))->~T();
                    s.live = false;
                    if (poisonOnFree) std::memset(s.data, kPoisonByte, sizeof(T));
                }
                s.next = m_free;
                m_free = &s;
            }
        }
        m_live = 0;
    }
    /// Clear(), then return all memory to the system.
    void Release() {
        Clear();
        m_blocks.clear();
        m_free = nullptr;
        m_capacity = 0;
    }

    std::size_t Live() const { return m_live; }
    std::size_t Capacity() const { return m_capacity; }
    std::size_t FreeSlots() const { return m_capacity - m_live; }
    std::size_t Limit() const { return m_limit; }
    AllocStats Stats() const {
        AllocStats s;
        s.used = m_live * sizeof(T);
        s.reserved = m_capacity * sizeof(Slot);
        s.limit = m_limit * sizeof(T);
        s.highWater = m_high * sizeof(T);
        s.live = m_live;
        s.allocations = m_allocs;
        s.failures = m_failures;
        return s;
    }

private:
    struct Slot {
        alignas(T) unsigned char data[sizeof(T)];
        Slot* next;
        bool live;
    };
    struct Block {
        std::unique_ptr<Slot[]> slots;
        std::size_t count = 0;
    };
    // `data` is the first member of a standard-layout Slot, so a T* is also a Slot*.
    static Slot* SlotOf(T* p) { return reinterpret_cast<Slot*>(p); }

    Slot* Acquire() {
        if (m_limit && m_live >= m_limit) return nullptr;
        if (!m_free && !AddBlock()) return nullptr;
        Slot* s = m_free;
        m_free = s->next;
        return s;
    }
    bool AddBlock() {
        std::size_t n = m_blockSize;
        if (m_limit) {
            const std::size_t room = m_limit > m_capacity ? m_limit - m_capacity : 0;
            if (room == 0) return false;
            if (n > room) n = room;
        }
        if (n > static_cast<std::size_t>(-1) / sizeof(Slot)) return false;
        try {
            Block b;
            b.slots.reset(new (std::nothrow) Slot[n]);
            if (!b.slots) return false;
            b.count = n;
            m_blocks.push_back(std::move(b));
        } catch (...) {
            return false;
        }
        Block& b = m_blocks.back();
        for (std::size_t i = n; i-- > 0;) {
            b.slots[i].live = false;
            b.slots[i].next = m_free;
            m_free = &b.slots[i];
        }
        m_capacity += n;
        return true;
    }

    std::size_t m_blockSize;
    std::size_t m_limit;
    std::vector<Block> m_blocks;
    Slot* m_free = nullptr;
    std::size_t m_capacity = 0, m_live = 0, m_high = 0;
    std::uint64_t m_allocs = 0, m_failures = 0;
};

} // namespace okay
