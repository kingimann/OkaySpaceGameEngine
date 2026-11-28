// okay-ms own <netdb.h>, written from POSIX.1-2017 (<netdb.h>): name and address lookup (getaddrinfo). Declarations only
// (see <sys/socket.h>); the flag and error numbers are our own choice (Round 35).
#pragma once
#include <netinet/in.h>
#define AI_PASSIVE 0x01
#define AI_CANONNAME 0x02
#define AI_NUMERICHOST 0x04
#define AI_NUMERICSERV 0x400
#define AI_ADDRCONFIG 0x20
#define NI_NUMERICHOST 0x01
#define NI_NUMERICSERV 0x02
#define NI_MAXHOST 1025
#define NI_MAXSERV 32
#define EAI_AGAIN -3
#define EAI_FAIL -4
#define EAI_NONAME -2
#define EAI_SYSTEM -11
struct addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    socklen_t ai_addrlen;
    struct sockaddr* ai_addr;
    char* ai_canonname;
    struct addrinfo* ai_next;
};
extern "C" {
int getaddrinfo(const char* node, const char* service, const struct addrinfo* hints, struct addrinfo** out);
void freeaddrinfo(struct addrinfo* list);
const char* gai_strerror(int code);
int getnameinfo(const struct sockaddr* addr, socklen_t size, char* host, socklen_t host_size, char* serv,
                socklen_t serv_size, int flags);
}
