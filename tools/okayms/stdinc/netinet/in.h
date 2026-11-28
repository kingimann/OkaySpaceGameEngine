// okay-ms own <netinet/in.h>, written from POSIX.1-2017 (<netinet/in.h>): the Internet address family's types. See
// <sys/socket.h> for what "declarations only" means here. IPPROTO_TCP 6 and IPPROTO_UDP 17 are RFC 790's protocol
// numbers; INADDR_ANY and INADDR_LOOPBACK are in host byte order, as the standard says (Round 35).
#pragma once
#include <sys/socket.h>
typedef unsigned short in_port_t;
typedef unsigned int in_addr_t;
#define IPPROTO_IP 0
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define IPPROTO_IPV6 41
#define INADDR_ANY ((in_addr_t)0x00000000)
#define INADDR_BROADCAST ((in_addr_t)0xffffffff)
#define INADDR_LOOPBACK ((in_addr_t)0x7f000001)
#define INET_ADDRSTRLEN 16
#define INET6_ADDRSTRLEN 46
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
extern "C" {
unsigned int htonl(unsigned int value);
unsigned short htons(unsigned short value);
unsigned int ntohl(unsigned int value);
unsigned short ntohs(unsigned short value);
}
