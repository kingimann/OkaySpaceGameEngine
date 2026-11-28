// okay-ms own <sys/socket.h>, written from POSIX.1-2017 (The Open Group Base Specifications Issue 7, <sys/socket.h>).
// For the engine's POSIX paths (the plain survey; UdpSocket.cpp, ServerQuery.cpp, OwnHttpNet.hpp). Declarations only, as
// <unistd.h>: the Microsoft C runtime has no POSIX sockets (Windows has Winsock, <winsock2.h>), so a program that calls
// one does not link. The numbers are our own choice, except AF_INET 2 and the socket types 1 and 2, which most systems
// share (Round 35).
#pragma once
#include <sys/types.h>
typedef unsigned int socklen_t;
typedef unsigned short sa_family_t;
#define AF_UNSPEC 0
#define AF_UNIX 1
#define AF_INET 2
#define AF_INET6 10
#define PF_INET AF_INET
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define SO_ERROR 4
#define SO_BROADCAST 6
#define SO_SNDBUF 7
#define SO_RCVBUF 8
#define SO_KEEPALIVE 9
#define SO_RCVTIMEO 20
#define SO_SNDTIMEO 21
#define MSG_PEEK 2
#define MSG_DONTWAIT 0x40
#define MSG_NOSIGNAL 0x4000
#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2
struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
};
// Large enough and aligned for every address of this header's families ([sys/socket.h]: sockaddr_storage).
struct sockaddr_storage {
    sa_family_t ss_family;
    char ss_pad[6];
    long long ss_align;
    char ss_rest[112];
};
extern "C" {
int socket(int domain, int type, int protocol);
int bind(int s, const struct sockaddr* addr, socklen_t size);
int connect(int s, const struct sockaddr* addr, socklen_t size);
int listen(int s, int backlog);
int accept(int s, struct sockaddr* addr, socklen_t* size);
int shutdown(int s, int how);
int getsockname(int s, struct sockaddr* addr, socklen_t* size);
int getpeername(int s, struct sockaddr* addr, socklen_t* size);
int setsockopt(int s, int level, int name, const void* value, socklen_t size);
int getsockopt(int s, int level, int name, void* value, socklen_t* size);
ssize_t send(int s, const void* data, unsigned long long size, int flags);
ssize_t recv(int s, void* data, unsigned long long size, int flags);
ssize_t sendto(int s, const void* data, unsigned long long size, int flags, const struct sockaddr* to, socklen_t to_size);
ssize_t recvfrom(int s, void* data, unsigned long long size, int flags, struct sockaddr* from, socklen_t* from_size);
}
