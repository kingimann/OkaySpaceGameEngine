#pragma once
// Replaces the program's global operator new / delete for a test: every allocation is counted on the thread that made it, and a
// test can arm a countdown on its own thread so that the n-th allocation from then on throws std::bad_alloc (fault injection).
// Include it in exactly ONE source file of a test program (it defines the replacement operators, which C++ allows once).
// Used by tests/test_jobs.cpp (fault injection) and okayui/tests/test_okayui_frame_budget.cpp (allocations per frame).
#include <cstdlib>
#include <new>

namespace allochook {
/// Allocations made on this thread so far.
inline thread_local long long count = 0;
/// > 0: the allocation that brings it to 0 throws std::bad_alloc (on this thread only); -1: off.
inline thread_local long failCountdown = -1;
inline void* Alloc(std::size_t n) {
    ++count;
    if (failCountdown > 0 && --failCountdown == 0) { failCountdown = -1; throw std::bad_alloc(); }
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
} // namespace allochook

void* operator new(std::size_t n) { return allochook::Alloc(n); }
void* operator new[](std::size_t n) { return allochook::Alloc(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
