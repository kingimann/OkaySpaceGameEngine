// FrameArena, ArenaAllocator, PoolAllocator, SlotMap / Handle and StringId.
#include "test_framework.hpp"
#include "okay/Core/Memory.hpp"
#include "okay/Core/SlotMap.hpp"
#include "okay/Core/StringId.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace okay;

namespace {

bool Aligned(const void* p, std::size_t a) { return (reinterpret_cast<std::uintptr_t>(p) & (a - 1)) == 0; }

// ---- FrameArena ---------------------------------------------------------------------

void TestArenaBasics() {
    RUN_SUITE("arena: allocation and alignment");
    FrameArena arena(4096);
    CHECK(arena.Used() == 0 && arena.Reserved() == 0 && arena.HighWater() == 0);   // nothing is allocated until needed
    void* a = arena.Allocate(10, 1);
    CHECK(a != nullptr && arena.Used() == 10);
    void* b = arena.Allocate(8, 8);
    CHECK(b != nullptr && Aligned(b, 8));
    CHECK(arena.Used() >= 18 && arena.Used() <= 18 + 8);

    // every power of two up to 4096, mixed with odd sizes; ranges never overlap, data survives
    struct Block { unsigned char* p; std::size_t n; unsigned char fill; };
    std::vector<Block> blocks;
    std::mt19937 rng(7);
    FrameArena big(1024);
    bool aligned = true;
    for (int i = 0; i < 400; ++i) {
        const std::size_t align = std::size_t(1) << (rng() % 13);   // 1 .. 4096
        const std::size_t n = 1 + rng() % 300;
        auto* p = static_cast<unsigned char*>(big.Allocate(n, align));
        if (!p) { aligned = false; break; }
        if (!Aligned(p, align)) aligned = false;
        const unsigned char fill = static_cast<unsigned char>(i);
        std::fill(p, p + n, fill);
        blocks.push_back({p, n, fill});
    }
    CHECK(aligned);
    bool intact = true;
    for (const Block& bl : blocks)
        for (std::size_t k = 0; k < bl.n; ++k) if (bl.p[k] != bl.fill) { intact = false; break; }
    CHECK(intact);
    std::vector<std::pair<std::uintptr_t, std::uintptr_t>> ranges;
    for (const Block& bl : blocks) ranges.emplace_back((std::uintptr_t)bl.p, (std::uintptr_t)bl.p + bl.n);
    std::sort(ranges.begin(), ranges.end());
    bool disjoint = true;
    for (std::size_t i = 1; i < ranges.size(); ++i) if (ranges[i].first < ranges[i - 1].second) disjoint = false;
    CHECK(disjoint);

    // zero bytes: a valid pointer; bad alignments are refused, not crashed on
    CHECK(arena.Allocate(0) != nullptr);
    const std::uint64_t failures = arena.Stats().failures;
    CHECK(arena.Allocate(16, 3) == nullptr && arena.Allocate(16, 0) == nullptr && arena.Allocate(16, 24) == nullptr);
    CHECK(arena.Stats().failures == failures + 3);

    // helpers
    int* ints = arena.AllocArray<int>(10);
    CHECK(ints && Aligned(ints, alignof(int)));
    for (int i = 0; i < 10; ++i) ints[i] = i;
    struct P { int x, y; P(int a, int b) : x(a), y(b) {} };
    P* pp = arena.Make<P>(3, 4);
    CHECK(pp && pp->x == 3 && pp->y == 4);
    const char* s = arena.CopyString("hello arena");
    CHECK(s && std::string(s) == "hello arena");
    const char* e = arena.CopyString("");
    CHECK(e && e[0] == '\0');
    double* z = static_cast<double*>(arena.AllocateZeroed(64, 16));
    bool zeros = z != nullptr;
    for (int i = 0; z && i < 8; ++i) zeros = zeros && z[i] == 0.0;
    CHECK(zeros && Aligned(z, 16));
}

void TestArenaLimitsAndFailure() {
    RUN_SUITE("arena: limit, growth, failure");
    FrameArena arena(256, 1000);                       // at most 1000 bytes per frame
    CHECK(arena.Limit() == 1000);
    void* p1 = arena.Allocate(600, 1);
    CHECK(p1 != nullptr);
    CHECK(arena.Allocate(500, 1) == nullptr);          // 600 + 500 > 1000
    CHECK(arena.Used() == 600);                        // a refusal changes nothing
    void* p2 = arena.Allocate(400, 1);                 // exactly the limit
    CHECK(p2 != nullptr && arena.Used() == 1000);
    CHECK(arena.Allocate(1, 1) == nullptr);
    CHECK(arena.Stats().failures == 2 && arena.HighWater() == 1000);
    CHECK(arena.Allocate(5000, 1) == nullptr);         // bigger than the limit by itself
    arena.Reset();
    CHECK(arena.Used() == 0 && arena.HighWater() == 1000);
    CHECK(arena.Allocate(1000, 1) != nullptr);         // a new frame, a new budget
    CHECK(arena.Allocate(1, 1) == nullptr);
    // padding counts towards the limit
    FrameArena tight(64, 100);
    CHECK(tight.Allocate(1, 1) != nullptr);
    CHECK(tight.Allocate(99, 64) == nullptr || tight.Used() <= 100);
    CHECK(tight.Used() <= 100);
    // the limit can change
    FrameArena lim(256);
    CHECK(lim.Allocate(300, 1) != nullptr);
    lim.SetLimit(310);
    CHECK(lim.Allocate(20, 1) == nullptr && lim.Allocate(10, 1) != nullptr);

    // absurd sizes fail cleanly (no throw, no crash)
    FrameArena any(128);
    CHECK(any.Allocate(static_cast<std::size_t>(-1), 1) == nullptr);
    CHECK(any.Allocate(static_cast<std::size_t>(-1) / 2, 16) == nullptr);
    CHECK(any.Allocate(static_cast<std::size_t>(1) << 60, 1) == nullptr);
    CHECK(any.AllocArray<int>(static_cast<std::size_t>(-1) / 2) == nullptr);
    CHECK(any.Stats().failures >= 4);
    CHECK(any.Allocate(16, 1) != nullptr);             // still usable

    // growth by chunks: earlier pointers stay valid, one chunk after Reset
    FrameArena grow(128);
    std::vector<unsigned char*> ps;
    for (int i = 0; i < 40; ++i) {
        auto* p = static_cast<unsigned char*>(grow.Allocate(100, 8));
        CHECK(p != nullptr);
        if (!p) break;
        std::fill(p, p + 100, static_cast<unsigned char>(i + 1));
        ps.push_back(p);
    }
    bool keep = true;
    for (std::size_t i = 0; i < ps.size(); ++i) for (int k = 0; k < 100; ++k) if (ps[i][k] != i + 1) keep = false;
    CHECK(keep);
    const std::size_t frameUsed = grow.Used();
    CHECK(frameUsed >= 4000 && grow.Reserved() >= frameUsed);
    grow.Reset();
    CHECK(grow.Used() == 0 && grow.Reserved() >= frameUsed);       // one chunk now, big enough for that frame
    const std::size_t reserved = grow.Reserved();
    for (int i = 0; i < 40; ++i) CHECK(grow.Allocate(100, 8) != nullptr);
    CHECK(grow.Reserved() == reserved);                             // the same frame needs no more memory
    grow.Reset();
    CHECK(grow.Reserved() == reserved);
    CHECK(grow.Stats().resets == 2 && grow.Stats().highWater >= frameUsed);
}

void TestArenaPoisonAndMarkers() {
    RUN_SUITE("arena: poison, markers, scopes");
    FrameArena arena(1024);
    arena.poisonOnReset = true;
    auto* p = static_cast<unsigned char*>(arena.Allocate(64, 1));
    std::fill(p, p + 64, static_cast<unsigned char>(0x11));
    arena.Reset();
    bool poisoned = true;                      // the single chunk is still the arena's memory
    for (int i = 0; i < 64; ++i) if (p[i] != kPoisonByte) poisoned = false;
    CHECK(poisoned);

    // Mark / Rewind
    arena.Reset();
    void* a = arena.Allocate(32, 8);
    FrameArena::Marker m = arena.Mark();
    void* b = arena.Allocate(100, 8);
    CHECK(arena.Used() >= 132);
    CHECK(arena.Rewind(m));
    CHECK(arena.Used() == m.used);
    void* b2 = arena.Allocate(100, 8);
    CHECK(b2 == b);                            // the space was handed out again
    (void)a;
    arena.Reset();
    CHECK(!arena.Rewind(m));                   // a marker from before a Reset is refused
    {
        void* keep = arena.Allocate(16, 8);
        const std::size_t before = arena.Used();
        {
            ArenaScope scope(arena);
            arena.Allocate(200, 8);
            CHECK(arena.Used() > before);
        }
        CHECK(arena.Used() == before);
        (void)keep;
    }
    // rewinding across chunks
    FrameArena small(64);
    small.poisonOnReset = true;
    small.Allocate(40, 1);
    FrameArena::Marker mm = small.Mark();
    for (int i = 0; i < 10; ++i) small.Allocate(60, 1);
    CHECK(small.Reserved() > 64 + 60);
    CHECK(small.Rewind(mm) && small.Used() == 40);
    void* again = small.Allocate(60, 1);       // reuses a chunk kept from before
    CHECK(again != nullptr && small.Used() == 100);
    // markers taken out of order are refused rather than corrupting the arena
    FrameArena ooo(256);
    ooo.Allocate(10, 1);
    FrameArena::Marker first = ooo.Mark();
    ooo.Allocate(50, 1);
    FrameArena::Marker second = ooo.Mark();
    CHECK(ooo.Rewind(first));
    CHECK(!ooo.Rewind(second) && ooo.Used() == first.used);

    // moving an arena keeps its memory and counters
    FrameArena src(512);
    int* v = src.AllocArray<int>(4);
    v[0] = 99;
    FrameArena dst(std::move(src));
    CHECK(dst.Used() >= 16 && v[0] == 99 && src.Used() == 0);
    CHECK(dst.Allocate(8, 8) != nullptr);
}

void TestArenaAllocator() {
    RUN_SUITE("arena: std allocator");
    FrameArena arena(4096, 2000);
    std::vector<int, ArenaAllocator<int>> v{ArenaAllocator<int>(&arena)};
    v.reserve(100);
    for (int i = 0; i < 100; ++i) v.push_back(i);
    CHECK(v.size() == 100 && v[99] == 99 && arena.Used() >= 400);
    bool threw = false;
    try {
        std::vector<char, ArenaAllocator<char>> huge{ArenaAllocator<char>(&arena)};
        huge.resize(100000);
    } catch (const std::bad_alloc&) { threw = true; }
    CHECK(threw);
    CHECK((ArenaAllocator<int>(&arena) == ArenaAllocator<double>(&arena)));
}

// ---- PoolAllocator --------------------------------------------------------------------

struct Counted {
    static inline int alive = 0, made = 0;
    int value;
    explicit Counted(int v = 0) : value(v) { ++alive; ++made; }
    Counted(const Counted& o) : value(o.value) { ++alive; ++made; }
    ~Counted() { --alive; }
};
struct alignas(64) Wide { char data[64]; int v = 0; };
struct Thrower {
    explicit Thrower(bool t) { if (t) throw std::runtime_error("ctor"); }
};

void TestPool() {
    RUN_SUITE("pool: allocator");
    Counted::alive = Counted::made = 0;
    {
        PoolAllocator<Counted> pool(8);
        CHECK(pool.Live() == 0 && pool.Capacity() == 0);   // no memory until the first Create
        std::vector<Counted*> ps;
        for (int i = 0; i < 100; ++i) { Counted* c = pool.Create(i); CHECK(c && c->value == i); ps.push_back(c); }
        CHECK(pool.Live() == 100 && Counted::alive == 100);
        CHECK(pool.Capacity() == 104 && pool.FreeSlots() == 4);    // 13 blocks of 8
        // addresses are stable and distinct
        std::unordered_set<Counted*> uniq(ps.begin(), ps.end());
        CHECK(uniq.size() == 100);
        std::vector<Counted*> copy = ps;
        for (int i = 100; i < 300; ++i) ps.push_back(pool.Create(i));
        bool stable = true;
        for (int i = 0; i < 100; ++i) if (ps[i] != copy[i] || ps[i]->value != i) stable = false;
        CHECK(stable);
        // free list: the slot just freed is the next one handed out
        Counted* victim = ps[17];
        CHECK(pool.Destroy(victim));
        CHECK(pool.Live() == 299 && Counted::alive == 299);
        Counted* reused = pool.Create(-1);
        CHECK(reused == victim && reused->value == -1);
        CHECK(!pool.Destroy(nullptr));
        CHECK(pool.Destroy(reused));
        CHECK(!pool.Destroy(reused));                       // a double Destroy is refused
        CHECK(pool.Live() == 299);
        CHECK(pool.Owns(ps[0]) && pool.IsLive(ps[0]) && !pool.IsLive(reused));
        Counted outsider(5);
        CHECK(!pool.Owns(&outsider));
        // ForEach sees exactly the live ones
        long sum = 0; int seen = 0;
        pool.ForEach([&](Counted& c) { sum += c.value; ++seen; });
        long want = 0;
        for (int i = 0; i < 300; ++i) if (i != 17) want += i;
        CHECK(seen == 299 && sum == want);
        const AllocStats st = pool.Stats();
        CHECK(st.live == 299 && st.allocations == 301 && st.highWater == 300 * sizeof(Counted));
        // Clear destructs everything but keeps the memory
        const std::size_t cap = pool.Capacity();
        pool.Clear();
        CHECK(pool.Live() == 0 && Counted::alive == 1 && pool.Capacity() == cap);   // 1 = `outsider`
        Counted* again = pool.Create(1);
        CHECK(again != nullptr && pool.Capacity() == cap);
        pool.Release();
        CHECK(pool.Capacity() == 0 && pool.Live() == 0);
        pool.Create(5);                                      // left alive: the destructor cleans it
    }
    CHECK(Counted::alive == 0);

    // limits
    PoolAllocator<int> small(4, 10);
    std::vector<int*> got;
    for (int i = 0; i < 10; ++i) { int* p = small.Create(i); CHECK(p != nullptr); got.push_back(p); }
    CHECK(small.Create(99) == nullptr && small.Stats().failures == 1);
    CHECK(small.Capacity() <= 10 && small.Capacity() >= 10);
    CHECK(small.Destroy(got[3]));
    int* p = small.Create(77);
    CHECK(p == got[3] && *p == 77);

    // a constructor that throws gives the slot back
    PoolAllocator<Thrower> tp(4);
    bool threw = false;
    try { tp.Create(true); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw && tp.Live() == 0 && tp.Stats().allocations == 0);
    Thrower* ok = tp.Create(false);
    CHECK(ok != nullptr && tp.Live() == 1);

    // over-aligned types
    PoolAllocator<Wide> wp(3);
    bool wideAligned = true;
    std::vector<Wide*> ws;
    for (int i = 0; i < 20; ++i) { Wide* w = wp.Create(); if (!w || !Aligned(w, 64)) wideAligned = false; else ws.push_back(w); }
    CHECK(wideAligned);

    // poison on free (the slot is still the pool's memory)
    PoolAllocator<std::uint64_t> pp(4);
    pp.poisonOnFree = true;
    auto* num = pp.Create(0x1122334455667788ull);
    unsigned char* raw = reinterpret_cast<unsigned char*>(num);
    CHECK(pp.Destroy(num));
    bool pois = true;
    for (int i = 0; i < 8; ++i) if (raw[i] != kPoisonByte) pois = false;
    CHECK(pois);

    // randomized against a model
    PoolAllocator<int> rp(16);
    std::set<int*> live;
    std::mt19937 rng(11);
    bool ok2 = true;
    for (int i = 0; i < 20000; ++i) {
        if (live.empty() || rng() % 3 != 0) {
            int* q = rp.Create(i);
            if (!q || live.count(q)) ok2 = false;
            else live.insert(q);
        } else {
            auto it = live.begin();
            std::advance(it, rng() % live.size());
            if (!rp.Destroy(*it)) ok2 = false;
            live.erase(it);
        }
        if (rp.Live() != live.size()) ok2 = false;
    }
    CHECK(ok2);
}

// ---- SlotMap / Handle ------------------------------------------------------------------

struct Thing { int id; std::string name; };
struct Other { int x; };

void TestSlotMap() {
    RUN_SUITE("slotmap: handles");
    static_assert(!std::is_same_v<Handle<Thing>, Handle<Other>>, "handles of different maps are different types");
    SlotMap<Thing> map;
    Handle<Thing> none;
    CHECK(!none.Valid() && !map.Contains(none) && map.Get(none) == nullptr);
    CHECK(map.Empty() && map.Size() == 0);

    auto h1 = map.Insert(Thing{1, "one"});
    auto h2 = map.Emplace(Thing{2, "two"});
    auto h3 = map.Insert(Thing{3, "three"});
    CHECK(h1.Valid() && h2.Valid() && h3.Valid() && h1 != h2 && h2 != h3);
    CHECK(map.Size() == 3 && map.Get(h2)->name == "two" && map[h3].id == 3 && map.At(h1).name == "one");

    CHECK(map.Erase(h2));
    CHECK(!map.Contains(h2) && map.Get(h2) == nullptr && map.Size() == 2);
    CHECK(!map.Erase(h2));                                // a stale handle does nothing
    bool threw = false;
    try { map.At(h2); } catch (const std::out_of_range&) { threw = true; }
    CHECK(threw);
    CHECK(map.Get(h1)->id == 1 && map.Get(h3)->id == 3);  // the others survived the swap-remove

    // the slot is reused, the old handle stays stale
    auto h4 = map.Insert(Thing{4, "four"});
    CHECK(h4.index == h2.index && h4.generation != h2.generation);
    CHECK(!map.Contains(h2) && map.Contains(h4) && map.Get(h4)->id == 4);
    CHECK(map.Get(Handle<Thing>(h4.index, h4.generation + 1)) == nullptr);   // a made-up generation is not alive
    CHECK(map.Get(Handle<Thing>(9999, 1)) == nullptr);                       // nor is an index out of range

    // raw round trip and hashing
    CHECK(Handle<Thing>::FromRaw(h4.Raw()) == h4);
    std::unordered_map<Handle<Thing>, int> byHandle;
    byHandle[h1] = 1; byHandle[h3] = 3; byHandle[h4] = 4;
    CHECK(byHandle.size() == 3 && byHandle[h3] == 3);

    // iteration: values, entries, constness
    int sum = 0;
    for (const Thing& t : map) sum += t.id;
    CHECK(sum == 1 + 3 + 4);
    int viaEntries = 0; bool consistent = true;
    for (auto [h, v] : map.Entries()) { viaEntries += v.id; if (map.Get(h) != &v) consistent = false; v.name += "!"; }
    CHECK(viaEntries == 8 && consistent && map[h1].name == "one!");
    const SlotMap<Thing>& cmap = map;
    int csum = 0;
    for (auto [h, v] : cmap.Entries()) { (void)h; csum += v.id; }
    CHECK(csum == 8 && cmap.Get(h1) != nullptr);
    for (std::size_t i = 0; i < map.Size(); ++i) CHECK(map.Get(map.HandleAt(i)) == &map.ValueAt(i));

    // EraseIf
    auto removed = map.EraseIf([](Handle<Thing>, Thing& t) { return t.id % 2 == 1; });
    CHECK(removed == 2 && map.Size() == 1 && map.Contains(h4) && !map.Contains(h1) && !map.Contains(h3));

    // Clear makes every handle stale, and the map is still usable
    auto h5 = map.Insert(Thing{5, "five"});
    map.Clear();
    CHECK(map.Empty() && !map.Contains(h4) && !map.Contains(h5));
    auto h6 = map.Insert(Thing{6, "six"});
    CHECK(map.Contains(h6) && !map.Contains(h4) && !map.Contains(h5) && map.Size() == 1);
    map.Reserve(1000);
    CHECK(map.Capacity() >= 1000 && map.Contains(h6));
}

struct MoveOnly {
    std::unique_ptr<int> p;
    explicit MoveOnly(int v) : p(new int(v)) {}
    MoveOnly(MoveOnly&&) = default;
    MoveOnly& operator=(MoveOnly&&) = default;
};
struct Boom {
    explicit Boom(bool t) { if (t) throw std::runtime_error("boom"); }
};

void TestSlotMapRandomized() {
    RUN_SUITE("slotmap: randomized against a model");
    SlotMap<long> map;
    std::map<std::uint64_t, long> model;          // live handles by raw value
    std::vector<Handle<long>> dead;
    std::mt19937 rng(2024);
    bool ok = true;
    for (int step = 0; step < 60000; ++step) {
        const unsigned op = rng() % 10;
        if (op < 5 || model.empty()) {
            const long v = (long)rng();
            auto h = map.Insert(v);
            if (!h.Valid() || model.count(h.Raw())) ok = false;
            model[h.Raw()] = v;
        } else if (op < 8) {
            auto it = model.begin();
            std::advance(it, rng() % model.size());
            auto h = Handle<long>::FromRaw(it->first);
            if (!map.Erase(h)) ok = false;
            dead.push_back(h);
            model.erase(it);
        } else {
            auto it = model.begin();
            std::advance(it, rng() % model.size());
            const long* p = map.Get(Handle<long>::FromRaw(it->first));
            if (!p || *p != it->second) ok = false;
        }
        if (map.Size() != model.size()) ok = false;
        if (step % 2000 == 0) {
            // every live handle works, every dead one is stale, iteration covers exactly the live values
            std::multiset<long> a, b;
            for (long v : map) a.insert(v);
            for (auto& kv : model) b.insert(kv.second);
            if (a != b) ok = false;
            for (auto& kv : model) { const long* p = map.Get(Handle<long>::FromRaw(kv.first)); if (!p || *p != kv.second) ok = false; }
            for (auto& h : dead) if (map.Contains(h)) ok = false;
        }
        if (!ok) break;
    }
    CHECK(ok);

    SlotMap<MoveOnly> mo;                         // move-only values work (swap-remove moves)
    auto a = mo.Emplace(1), b = mo.Emplace(2), c = mo.Emplace(3);
    CHECK(mo.Erase(a) && *mo[c].p == 3 && *mo[b].p == 2);

    SlotMap<Boom> bm;                             // a throwing constructor leaves the map as it was
    bm.Emplace(false);
    bool threw = false;
    try { bm.Emplace(true); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw && bm.Size() == 1);
    auto after = bm.Emplace(false);
    CHECK(bm.Size() == 2 && bm.Contains(after));
}

// ---- StringId --------------------------------------------------------------------------

constexpr StringId kJump = "player.jump"_sid;
static_assert("a"_sid.Hash() == 0xaf63dc4c8601ec8cull, "FNV-1a 64 of \"a\"");
static_assert("foobar"_sid.Hash() == 0x85944171f73967e8ull, "FNV-1a 64 of \"foobar\"");
static_assert(StringId().Hash() == 0 && ""_sid == StringId(), "the empty string is none");
static_assert(kJump == StringId::FromString("player.jump") && kJump != "player.run"_sid, "");

int g_collisions = 0;
std::string g_cA, g_cB;
StringId::Value g_cHash = 0;
void OnCollision(std::string_view a, std::string_view b, StringId::Value h) { ++g_collisions; g_cA = std::string(a); g_cB = std::string(b); g_cHash = h; }

void TestStringId() {
    RUN_SUITE("stringid");
    StringId::ClearTable();
    CHECK(!StringId() && StringId().Empty() && kJump && !kJump.Empty());
    switch (kJump.Hash()) { case "player.jump"_sid.Hash(): CHECK(true); break; default: CHECK(false); }   // usable in a case label
    CHECK(StringId::HashOf("abc") == "abc"_sid.Hash());

    // interning
    CHECK(!kJump.IsInterned() && kJump.Name().rfind("#", 0) == 0);
    StringId j = StringId::Intern("player.jump");
    CHECK(j == kJump && j.IsInterned() && j.Name() == "player.jump");
    CHECK(kJump.Name() == "player.jump");                // an id from the constexpr route finds its text too
    CHECK(StringId::Intern("player.jump") == j && StringId::InternedCount() == 1);
    CHECK(StringId::Intern("").Empty() && StringId::InternedCount() == 1 && StringId().Name().empty());
    CHECK(StringId::Make("x.y") == "x.y"_sid);

    // distinct strings, distinct hashes (a big sample)
    std::unordered_set<StringId::Value> seen;
    bool distinct = true;
    for (int i = 0; i < 100000; ++i) if (!seen.insert(StringId::HashOf("item_" + std::to_string(i))).second) distinct = false;
    CHECK(distinct);

    // std::hash and ordering
    std::unordered_map<StringId, int> m;
    m["a"_sid] = 1; m["b"_sid] = 2;
    CHECK(m["a"_sid] == 1 && m.size() == 2);
    std::set<StringId> ordered{"c"_sid, "a"_sid, "b"_sid};
    CHECK(ordered.size() == 3);

    // collision detection (forced with InternWithHash, there being no real pair to hand)
    StringId::ClearTable();
    g_collisions = 0;
    StringId::SetCollisionHandler(&OnCollision);
    StringId::SetCollisionCheck(true);
    CHECK(StringId::CollisionCheck());
    StringId first = StringId::InternWithHash(42, "first");
    StringId again = StringId::InternWithHash(42, "first");          // the same text is not a collision
    CHECK(first == again && StringId::CollisionCount() == 0 && g_collisions == 0);
    StringId second = StringId::InternWithHash(42, "second");
    CHECK(second == first && StringId::CollisionCount() == 1 && g_collisions == 1);
    CHECK(g_cA == "first" && g_cB == "second" && g_cHash == 42);
    CHECK(first.Name() == "first");                                   // the first text stays
    StringId::SetCollisionCheck(false);
    StringId::InternWithHash(42, "third");
    CHECK(StringId::CollisionCount() == 1 && g_collisions == 1);      // not checked, not counted
    StringId::SetCollisionCheck(true);
    StringId::SetCollisionHandler(nullptr);
    CHECK(StringId::InternWithHash(0, "ignored").Empty());
    StringId::ClearTable();
    CHECK(StringId::InternedCount() == 0 && StringId::CollisionCount() == 0);

    // interning from several threads at once
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t)
        ts.emplace_back([] { for (int i = 0; i < 2000; ++i) StringId::Intern("shared_" + std::to_string(i % 500)); });
    for (auto& t : ts) t.join();
    CHECK(StringId::InternedCount() == 500);
    CHECK(StringId::Intern("shared_7").Name() == "shared_7" && StringId::CollisionCount() == 0);
}

} // namespace

int main() {
    RUN_SUITE("arena_handles");
    TestArenaBasics();
    TestArenaLimitsAndFailure();
    TestArenaPoisonAndMarkers();
    TestArenaAllocator();
    TestPool();
    TestSlotMap();
    TestSlotMapRandomized();
    TestStringId();
    TEST_MAIN_RESULT();
}
