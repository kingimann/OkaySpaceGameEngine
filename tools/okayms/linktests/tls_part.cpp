// The second object of link_thread_local (Round 35): thread_local objects of class type in a file without main. Their
// constructors run for each thread from okay-ms's runtime TLS callback (runtime/startup.c, __okay_tls_callback_rt),
// because the file with main names no thread_local and so has no callback of its own; their destructors run when a
// thread ends. ISO/IEC 14882 [basic.stc.thread], [basic.start.dynamic] 7.
#include <string>
#include <vector>

namespace part {
int g_made = 0;
int g_gone = 0;
struct Probe {
    int alive;
    Probe() : alive(42) { ++g_made; }
    ~Probe() { ++g_gone; alive = 0; }
};
thread_local Probe t_probe;
thread_local std::string t_text("start");
thread_local std::vector<int> t_list;

// Appends to this thread's text and list and returns their sizes as one number (text * 100 + list).
int Touch(const char* add, int value) {
    t_text += add;
    t_list.push_back(value);
    return t_probe.alive == 42 ? static_cast<int>(t_text.size()) * 100 + static_cast<int>(t_list.size()) : -1;
}
}  // namespace part
