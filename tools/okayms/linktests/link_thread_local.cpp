// Link-and-run test of thread_local objects in another object file (Round 35; see tls_part.cpp). Built by okay-cl
// (/O0 /O1 /O2) with okay-ms's runtime and by MinGW g++, run under Wine; the exit code and the printed text must be the
// same. Each thread sees its own objects, made before it uses them.
#include "mini.hpp"
#include <windows.h>

namespace part {
extern int g_made;
extern int g_gone;
int Touch(const char* add, int value);
}  // namespace part

static unsigned long __stdcall Worker(void* out) {
    *static_cast<int*>(out) = part::Touch("ab", 1);
    return 0;
}

int main() {
    RUN_SUITE("thread_local");
    CHECK(part::Touch("x", 5) == 601);  // "startx", one item
    CHECK(part::Touch("yz", 6) == 802);
    int theirs = 0;
    HANDLE h = CreateThread(nullptr, 0, Worker, &theirs, 0, nullptr);
    CHECK(h != nullptr);
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
    CHECK(theirs == 701);  // the worker's own "startab", one item
    CHECK(part::Touch("", 7) == 803);  // this thread's objects were not touched by the worker
    CHECK(part::g_made >= 2);
    CHECK(part::g_gone >= 1);  // the worker's object was destroyed when it ended
    std::printf("main %d, worker %d\n", part::Touch("", 8), theirs);
    TEST_MAIN_RESULT();
}
