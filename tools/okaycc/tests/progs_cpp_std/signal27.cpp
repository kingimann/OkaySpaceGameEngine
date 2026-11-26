// Signals ([csignal.syn], ISO C 7.14, POSIX sigaction): a handler installed with std::signal runs
// for std::raise and returns into the program; SIG_IGN ignores; a blocked signal waits in the mask
// and is taken by sigtimedwait; SA_SIGINFO handlers get the signal number.
#include <csignal>
#include <cstdio>
#include <ctime>
static volatile std::sig_atomic_t g_hits = 0;
static volatile int g_last = 0;
static void OnSignal(int s) { g_hits = g_hits + 1; g_last = s; }
static void OnInfo(int s, siginfo_t *info, void *) { g_last = s * 100 + (info ? info->si_signo : 0); }
int main() {
    if (std::signal(SIGUSR1, OnSignal) == SIG_ERR) return 1;
    std::raise(SIGUSR1);
    std::raise(SIGUSR1);
    std::printf("%d %d\n", (int)g_hits, g_last);
    std::signal(SIGUSR2, SIG_IGN);
    std::raise(SIGUSR2);
    std::printf("still here %d\n", (int)g_hits);
    void (*prev)(int) = std::signal(SIGUSR1, SIG_DFL);
    std::printf("%d\n", prev == OnSignal);
    struct sigaction a;
    a.sa_handler = nullptr;
    a.sa_sigaction = OnInfo;
    sigemptyset(&a.sa_mask);
    a.sa_flags = SA_SIGINFO;
    sigaction(SIGTERM, &a, nullptr);
    std::raise(SIGTERM);
    std::printf("%d\n", g_last);
    sigset_t s, old;
    sigemptyset(&s);
    sigaddset(&s, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &s, &old);
    std::raise(SIGPIPE);
    struct timespec z = {0, 0};
    int got = sigtimedwait(&s, nullptr, &z);
    pthread_sigmask(SIG_SETMASK, &old, nullptr);
    std::printf("%d %d %d\n", got, sigismember(&s, SIGPIPE), sigismember(&old, SIGPIPE));
    return 0;
}
