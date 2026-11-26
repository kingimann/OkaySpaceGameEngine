/* <netinet/in.h> for okay-cc (POSIX <netinet/in.h>, ip(7), ipv6(7), Linux x86-64 values). Our own
 * header. Addresses and ports in these structures are in network byte order (big-endian). */
#pragma once
#include <sys/socket.h>
typedef unsigned short in_port_t;
typedef unsigned int in_addr_t;
struct in_addr {
    in_addr_t s_addr;
};
struct sockaddr_in {
    sa_family_t sin_family;
    in_port_t sin_port;
    struct in_addr sin_addr;
    unsigned char sin_zero[8];
};
struct in6_addr {
    unsigned char s6_addr[16];
};
struct sockaddr_in6 {
    sa_family_t sin6_family;
    in_port_t sin6_port;
    unsigned int sin6_flowinfo;
    struct in6_addr sin6_addr;
    unsigned int sin6_scope_id;
};
#define IPPROTO_IP 0
#define IPPROTO_ICMP 1
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define IPPROTO_IPV6 41
#define INADDR_ANY ((in_addr_t)0x00000000)
#define INADDR_BROADCAST ((in_addr_t)0xffffffff)
#define INADDR_NONE ((in_addr_t)0xffffffff)
#define INADDR_LOOPBACK ((in_addr_t)0x7f000001)
#define INET_ADDRSTRLEN 16
#define INET6_ADDRSTRLEN 46
#define IP_TOS 1
#define IP_TTL 2
#define IP_MULTICAST_TTL 33
#define IP_MULTICAST_LOOP 34
#define IP_ADD_MEMBERSHIP 35
#define IPV6_V6ONLY 26
#ifdef __cplusplus
extern "C" {
#endif
unsigned short htons(unsigned short v);
unsigned short ntohs(unsigned short v);
unsigned int htonl(unsigned int v);
unsigned int ntohl(unsigned int v);
#ifdef __cplusplus
}
#endif
