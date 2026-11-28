// okay-ms own <poll.h>, written from POSIX.1-2017 (<poll.h>). Declarations only (see <sys/socket.h>); the event bits are
// our own choice (Round 35).
#pragma once
typedef unsigned long long nfds_t;
#define POLLIN 0x001
#define POLLPRI 0x002
#define POLLOUT 0x004
#define POLLERR 0x008
#define POLLHUP 0x010
#define POLLNVAL 0x020
struct pollfd {
    int fd;
    short events;
    short revents;
};
extern "C" int poll(struct pollfd* fds, nfds_t count, int timeout_ms);
