/* Signals for okay-cc programs (ISO C 7.14, POSIX sigaction and sigprocmask), written from the
 * Linux x86-64 system call interface: rt_sigaction takes the kernel's struct (handler, flags,
 * restorer, mask) and the mask size (8). A handler returns into the restorer, which must make the
 * rt_sigreturn call (15) with the stack as the kernel left it: __okay_sigreturn is that sequence
 * (it first undoes its function's own frame). Our own code. */
#include <signal.h>
long syscall(long n, long a, long b, long c, long d);
void __okay_sigreturn(void);
int *__okay_errno(void);
#define SA_RESTORER 0x04000000

struct __okay_ksigaction {
    void *handler;
    unsigned long flags;
    void *restorer;
    unsigned long mask;
};

static void __okay_restore(void) { __okay_sigreturn(); }

static int __okay_fail(long r) {
    if (r < 0 && r > -4096) {
        *__okay_errno() = (int)-r;
        return -1;
    }
    return (int)r;
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *old) {
    struct __okay_ksigaction k, ko;
    long r;
    if (act) {
        k.handler = (act->sa_flags & SA_SIGINFO) ? (void *)act->sa_sigaction : (void *)act->sa_handler;
        k.flags = (unsigned long)(unsigned int)act->sa_flags | SA_RESTORER;
        k.restorer = (void *)__okay_restore;
        k.mask = act->sa_mask;
    }
    r = syscall(13, sig, act ? (long)&k : 0, old ? (long)&ko : 0, 8);
    if (r < 0) return __okay_fail(r);
    if (old) {
        old->sa_handler = (__okay_sighandler_t)ko.handler;
        old->sa_sigaction = (void (*)(int, siginfo_t *, void *))ko.handler;
        old->sa_mask = ko.mask;
        old->sa_flags = (int)(ko.flags & ~(unsigned long)SA_RESTORER);
    }
    return 0;
}

/* signal(): the handler stays installed and interrupted calls go on (SA_RESTART), as with glibc. */
__okay_sighandler_t signal(int sig, __okay_sighandler_t handler) {
    struct sigaction a, old;
    a.sa_handler = handler;
    a.sa_sigaction = 0;
    a.sa_mask = 0;
    a.sa_flags = SA_RESTART;
    if (sigaction(sig, &a, &old) < 0) return SIG_ERR;
    return old.sa_handler;
}

int kill(pid_t pid, int sig) { return __okay_fail(syscall(62, pid, sig, 0, 0)); }

int raise(int sig) {
    long pid = syscall(39, 0, 0, 0, 0);   /* getpid */
    long tid = syscall(186, 0, 0, 0, 0);  /* gettid */
    return __okay_fail(syscall(234, pid, tid, sig, 0)) == 0 ? 0 : -1; /* tgkill: to this thread */
}

int sigprocmask(int how, const sigset_t *set, sigset_t *old) {
    return __okay_fail(syscall(14, how, (long)set, (long)old, 8));
}
int pthread_sigmask(int how, const sigset_t *set, sigset_t *old) {
    long r = syscall(14, how, (long)set, (long)old, 8);
    return r < 0 ? (int)-r : 0;
}
int sigtimedwait(const sigset_t *set, siginfo_t *info, const struct timespec *timeout) {
    return __okay_fail(syscall(128, (long)set, (long)info, (long)timeout, 8));
}

int sigemptyset(sigset_t *set) {
    *set = 0;
    return 0;
}
int sigfillset(sigset_t *set) {
    *set = ~0UL;
    return 0;
}
int sigaddset(sigset_t *set, int sig) {
    if (sig < 1 || sig > 64) return -1;
    *set |= 1UL << (sig - 1);
    return 0;
}
int sigdelset(sigset_t *set, int sig) {
    if (sig < 1 || sig > 64) return -1;
    *set &= ~(1UL << (sig - 1));
    return 0;
}
int sigismember(const sigset_t *set, int sig) {
    if (sig < 1 || sig > 64) return -1;
    return (int)((*set >> (sig - 1)) & 1UL);
}
