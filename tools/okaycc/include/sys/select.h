/* <sys/select.h> for okay-cc (POSIX <sys/select.h>, select(2), Linux x86-64: 1024 descriptors in
 * 16 longs). Our own header; select is in lib/net.c. */
#pragma once
#include <sys/types.h>
#ifndef __okay_timeval_defined
#define __okay_timeval_defined
struct timeval {
    long tv_sec;
    long tv_usec;
};
#endif
#define FD_SETSIZE 1024
typedef struct {
    unsigned long fds_bits[16];
} fd_set;
#define FD_ZERO(s) __okay_fd_zero(s)
#define FD_SET(fd, s) ((s)->fds_bits[(fd) / 64] |= 1UL << ((fd) % 64))
#define FD_CLR(fd, s) ((s)->fds_bits[(fd) / 64] &= ~(1UL << ((fd) % 64)))
#define FD_ISSET(fd, s) (((s)->fds_bits[(fd) / 64] >> ((fd) % 64)) & 1UL)
#ifdef __cplusplus
extern "C" {
#endif
void __okay_fd_zero(fd_set *s);
int select(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *timeout);
#ifdef __cplusplus
}
#endif
