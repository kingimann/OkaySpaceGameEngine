#include "okay/Core/StringId.hpp"
#include "CoreThreads.hpp"

#include <atomic>
#include <cstdio>
#include <unordered_map>

namespace okay {

namespace {

struct Table {
    corethreads::Mutex mu;
    std::unordered_map<StringId::Value, std::string> names;   // node based: entries never move
    StringId::CollisionHandler handler = nullptr;
    std::size_t collisions = 0;
};

Table& GetTable() {
    static Table* t = new Table;   // never destroyed: ids may be looked up during static destruction
    return *t;
}

#ifdef NDEBUG
std::atomic<bool> g_check{false};
#else
std::atomic<bool> g_check{true};
#endif

} // namespace

StringId StringId::Intern(std::string_view s) { return InternWithHash(HashOf(s), s); }

StringId StringId::InternWithHash(Value h, std::string_view s) {
    if (h == 0) return StringId(0);
    Table& t = GetTable();
    CollisionHandler handler = nullptr;
    std::string existing;
    {
        corethreads::LockGuard g(t.mu);
        auto it = t.names.find(h);
        if (it == t.names.end()) {
            t.names.emplace(h, std::string(s));
            return StringId(h);
        }
        if (!g_check.load(std::memory_order_relaxed) || it->second == s) return StringId(h);
        ++t.collisions;
        handler = t.handler;
        existing = it->second;
    }
    if (handler) handler(existing, s, h);
#ifndef NDEBUG
    else std::fprintf(stderr, "[okay] StringId collision: \"%s\" and \"%.*s\" both hash to %llu\n",
                      existing.c_str(), (int)s.size(), s.data(), (unsigned long long)h);
#endif
    return StringId(h);
}

std::string StringId::Name() const {
    if (m_value == 0) return std::string();
    Table& t = GetTable();
    {
        corethreads::LockGuard g(t.mu);
        auto it = t.names.find(m_value);
        if (it != t.names.end()) return it->second;
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "#%llx", (unsigned long long)m_value);
    return buf;
}

bool StringId::IsInterned() const {
    if (m_value == 0) return false;
    Table& t = GetTable();
    corethreads::LockGuard g(t.mu);
    return t.names.count(m_value) != 0;
}

void StringId::SetCollisionHandler(CollisionHandler handler) {
    Table& t = GetTable();
    corethreads::LockGuard g(t.mu);
    t.handler = handler;
}

void StringId::SetCollisionCheck(bool on) { g_check.store(on, std::memory_order_relaxed); }
bool StringId::CollisionCheck() { return g_check.load(std::memory_order_relaxed); }

std::size_t StringId::CollisionCount() {
    Table& t = GetTable();
    corethreads::LockGuard g(t.mu);
    return t.collisions;
}

std::size_t StringId::InternedCount() {
    Table& t = GetTable();
    corethreads::LockGuard g(t.mu);
    return t.names.size();
}

void StringId::ClearTable() {
    Table& t = GetTable();
    corethreads::LockGuard g(t.mu);
    t.names.clear();
    t.collisions = 0;
}

} // namespace okay
