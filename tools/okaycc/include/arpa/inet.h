/* <arpa/inet.h> for okay-cc (POSIX <arpa/inet.h>, inet_pton(3), inet_ntop(3)). Our own header;
 * the functions are in lib/net.c (IPv4 text forms; IPv6 is not done). */
#pragma once
#include <netinet/in.h>
#ifdef __cplusplus
extern "C" {
#endif
int inet_pton(int family, const char *text, void *out);
const char *inet_ntop(int family, const void *addr, char *out, socklen_t size);
in_addr_t inet_addr(const char *text);
char *inet_ntoa(struct in_addr a);
#ifdef __cplusplus
}
#endif
