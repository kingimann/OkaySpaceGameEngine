// okay-ms own <sys/select.h>, written from POSIX.1-2017 (<sys/select.h>): select and its descriptor sets, a bit per
// descriptor below FD_SETSIZE. The fd_set helpers are our own inline code (they are macros in the standard). Declarations
// only for select itself (see <sys/socket.h>; Round 35).
#pragma once
#include <sys/types.h>
#include <ctime>
#define FD_SETSIZE 1024
struct timeval {
    long long tv_sec;
    long long tv_usec;
};
typedef struct {
    unsigned long long fds_bits[FD_SETSIZE / 64];
} fd_set;
inline void okay_fd_change__(int fd, fd_set* set, bool on) {
    if (fd < 0 || fd >= FD_SETSIZE) return;
    const unsigned long long bit = 1ull << (fd % 64);
    if (on) set->fds_bits[fd / 64] |= bit;
    else set->fds_bits[fd / 64] &= ~bit;
}
inline int okay_fd_isset__(int fd, const fd_set* set) {
    return fd >= 0 && fd < FD_SETSIZE && (set->fds_bits[fd / 64] >> (fd % 64)) & 1ull;
}
#define FD_ZERO(set) okay_fd_zero__(set)
inline void okay_fd_zero__(fd_set* set) {
    for (int i = 0; i < FD_SETSIZE / 64; ++i) set->fds_bits[i] = 0;
}
#define FD_SET(fd, set) okay_fd_change__((int)(fd), (set), true)
#define FD_CLR(fd, set) okay_fd_change__((int)(fd), (set), false)
#define FD_ISSET(fd, set) okay_fd_isset__((int)(fd), (set))
extern "C" int select(int count, fd_set* readable, fd_set* writable, fd_set* failed, struct timeval* timeout);
