// okay-ms own <arpa/inet.h>, written from POSIX.1-2017 (<arpa/inet.h>): the byte-order functions (from
// <netinet/in.h>) and the address text conversions. Declarations only (see <sys/socket.h>; Round 35).
#pragma once
#include <netinet/in.h>
extern "C" {
in_addr_t inet_addr(const char* text);
char* inet_ntoa(struct in_addr addr);
const char* inet_ntop(int family, const void* addr, char* out, socklen_t size);
int inet_pton(int family, const char* text, void* out);
}
