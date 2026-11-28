// okay-ms own <winsock2.h>: the Windows Sockets 2 names the engine's UDP code uses (Net/UdpSocket.cpp,
// Net/ServerQuery.cpp), declared in our own words from Microsoft's public Winsock documentation (WSAStartup, WSADATA,
// socket, bind, connect, sendto, recvfrom, select and fd_set, ioctlsocket and FIONBIO, WSAIoctl, setsockopt and
// SO_BROADCAST, getsockname, closesocket, listen, accept, shutdown, getpeername, the byte-order functions, sockaddr and sockaddr_in, the Windows Sockets error
// codes). The numbers are the documented values (address family AF_INET 2, protocol IPPROTO_UDP 17 as in RFC 790,
// WSAEWOULDBLOCK 10035 and the rest). No text from the Windows SDK, MinGW or Wine headers. The functions live in
// ws2_32.dll. Add a name here when an engine file needs it.
#pragma once
#include "windows.h"

typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;
typedef UINT_PTR SOCKET;

#define INVALID_SOCKET ((SOCKET)(~0ull))
#define SOCKET_ERROR (-1)

// Address families, socket types and protocols.
#define AF_UNSPEC 0
#define AF_INET 2
#define AF_INET6 23
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define INADDR_ANY ((u_long)0x00000000)
#define INADDR_LOOPBACK 0x7f000001
#define INADDR_BROADCAST ((u_long)0xffffffff)

// Socket options (setsockopt, level SOL_SOCKET).
#define SOL_SOCKET 0xffff
#define SO_REUSEADDR 0x0004
#define SO_ERROR 0x1007
#define SO_BROADCAST 0x0020
#define SO_SNDBUF 0x1001
#define SO_RCVBUF 0x1002

// ioctlsocket and WSAIoctl codes: the high bits say the direction, IOC_VENDOR marks a vendor code.
#define IOC_VOID 0x20000000
#define IOC_OUT 0x40000000
#define IOC_IN 0x80000000
#define IOC_VENDOR 0x18000000
#define _WSAIOW(group, number) (IOC_IN | (group) | (number))
#define FIONBIO 0x8004667E  // a u_long in: nonzero makes the socket non-blocking

// Windows Sockets error codes (WSAGetLastError).
#define WSAEINTR 10004
#define WSAEWOULDBLOCK 10035
#define WSAEINPROGRESS 10036
#define WSAEALREADY 10037
#define WSAEMSGSIZE 10040
#define WSAEADDRINUSE 10048
#define WSAENETRESET 10052
#define WSAECONNRESET 10054
#define WSAENOTCONN 10057
#define WSAETIMEDOUT 10060
#define WSAECONNREFUSED 10061

#define WSADESCRIPTION_LEN 256
#define WSASYS_STATUS_LEN 128

// What WSAStartup reports. The member order is the one documented for 64-bit Windows.
typedef struct WSAData {
    WORD wVersion;
    WORD wHighVersion;
    unsigned short iMaxSockets;
    unsigned short iMaxUdpDg;
    char* lpVendorInfo;
    char szDescription[WSADESCRIPTION_LEN + 1];
    char szSystemStatus[WSASYS_STATUS_LEN + 1];
} WSADATA;
typedef WSADATA* LPWSADATA;

struct in_addr {
    union {
        struct {
            u_char s_b1;
            u_char s_b2;
            u_char s_b3;
            u_char s_b4;
        } S_un_b;
        struct {
            u_short s_w1;
            u_short s_w2;
        } S_un_w;
        u_long S_addr;
    } S_un;
};
#define s_addr S_un.S_addr

struct sockaddr {
    u_short sa_family;
    char sa_data[14];
};
typedef struct sockaddr SOCKADDR;

struct sockaddr_in {
    short sin_family;
    u_short sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
};
typedef struct sockaddr_in SOCKADDR_IN;

struct timeval {
    long tv_sec;
    long tv_usec;
};

// select's socket sets: a count and up to FD_SETSIZE sockets.
#define FD_SETSIZE 64
typedef struct fd_set {
    u_int fd_count;
    SOCKET fd_array[FD_SETSIZE];
} fd_set;
inline void okay_fd_set_add__(SOCKET s, fd_set* set) {
    for (u_int i = 0; i < set->fd_count; ++i)
        if (set->fd_array[i] == s) return;
    if (set->fd_count < FD_SETSIZE) set->fd_array[set->fd_count++] = s;
}
inline int okay_fd_isset__(SOCKET s, const fd_set* set) {
    for (u_int i = 0; i < set->fd_count; ++i)
        if (set->fd_array[i] == s) return 1;
    return 0;
}
#define FD_ZERO(set) ((set)->fd_count = 0)
#define FD_SET(s, set) okay_fd_set_add__((SOCKET)(s), (set))
#define FD_ISSET(s, set) okay_fd_isset__((SOCKET)(s), (set))

typedef struct _OVERLAPPED* LPWSAOVERLAPPED;
typedef void (*LPWSAOVERLAPPED_COMPLETION_ROUTINE)(DWORD error, DWORD bytes, LPWSAOVERLAPPED overlapped, DWORD flags);

#define OKAY_WS2_IMPORT extern "C" __declspec(dllimport)

OKAY_WS2_IMPORT int WINAPI WSAStartup(WORD version, LPWSADATA data);
OKAY_WS2_IMPORT int WINAPI WSACleanup(void);
OKAY_WS2_IMPORT int WINAPI WSAGetLastError(void);
OKAY_WS2_IMPORT int WINAPI WSAIoctl(SOCKET s, DWORD code, LPVOID in, DWORD in_size, LPVOID out, DWORD out_size,
                                    LPDWORD returned, LPWSAOVERLAPPED overlapped,
                                    LPWSAOVERLAPPED_COMPLETION_ROUTINE routine);
OKAY_WS2_IMPORT SOCKET WINAPI socket(int family, int type, int protocol);
OKAY_WS2_IMPORT int WINAPI bind(SOCKET s, const struct sockaddr* name, int name_size);
OKAY_WS2_IMPORT int WINAPI connect(SOCKET s, const struct sockaddr* name, int name_size);
// TCP servers (Round 37, Automation/Link.cpp): listen, accept and shutdown, from their Winsock reference pages.
#define SOMAXCONN 0x7fffffff
#define SD_RECEIVE 0
#define SD_SEND 1
#define SD_BOTH 2
#define TCP_NODELAY 0x0001
OKAY_WS2_IMPORT int WINAPI listen(SOCKET s, int backlog);
OKAY_WS2_IMPORT SOCKET WINAPI accept(SOCKET s, struct sockaddr* address, int* address_size);
OKAY_WS2_IMPORT int WINAPI shutdown(SOCKET s, int how);
OKAY_WS2_IMPORT int WINAPI getpeername(SOCKET s, struct sockaddr* name, int* name_size);
OKAY_WS2_IMPORT int WINAPI closesocket(SOCKET s);
OKAY_WS2_IMPORT int WINAPI ioctlsocket(SOCKET s, long command, u_long* argument);
OKAY_WS2_IMPORT int WINAPI setsockopt(SOCKET s, int level, int name, const char* value, int value_size);
OKAY_WS2_IMPORT int WINAPI getsockopt(SOCKET s, int level, int name, char* value, int* value_size);
OKAY_WS2_IMPORT int WINAPI getsockname(SOCKET s, struct sockaddr* name, int* name_size);
OKAY_WS2_IMPORT int WINAPI sendto(SOCKET s, const char* data, int size, int flags, const struct sockaddr* to, int to_size);
OKAY_WS2_IMPORT int WINAPI recvfrom(SOCKET s, char* buffer, int size, int flags, struct sockaddr* from, int* from_size);
OKAY_WS2_IMPORT int WINAPI send(SOCKET s, const char* data, int size, int flags);
OKAY_WS2_IMPORT int WINAPI recv(SOCKET s, char* buffer, int size, int flags);
OKAY_WS2_IMPORT int WINAPI select(int ignored, fd_set* readable, fd_set* writable, fd_set* failed, const struct timeval* timeout);
OKAY_WS2_IMPORT u_short WINAPI htons(u_short value);
OKAY_WS2_IMPORT u_long WINAPI htonl(u_long value);
OKAY_WS2_IMPORT u_short WINAPI ntohs(u_short value);
OKAY_WS2_IMPORT u_long WINAPI ntohl(u_long value);
