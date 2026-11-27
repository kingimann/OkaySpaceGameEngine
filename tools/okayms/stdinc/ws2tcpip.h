// okay-ms own <ws2tcpip.h>: name resolution for the engine's UDP code (getaddrinfo, freeaddrinfo and the ADDRINFOA
// record), declared in our own words from Microsoft's public Winsock documentation. No text from the Windows SDK,
// MinGW or Wine headers. The functions live in ws2_32.dll.
#pragma once
#include <cstddef>
#include "winsock2.h"

typedef struct addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    size_t ai_addrlen;
    char* ai_canonname;
    struct sockaddr* ai_addr;
    struct addrinfo* ai_next;
} ADDRINFOA;
typedef ADDRINFOA* PADDRINFOA;

#define AI_PASSIVE 0x01
#define AI_CANONNAME 0x02
#define AI_NUMERICHOST 0x04

OKAY_WS2_IMPORT int WINAPI getaddrinfo(const char* node, const char* service, const ADDRINFOA* hints, PADDRINFOA* result);
OKAY_WS2_IMPORT void WINAPI freeaddrinfo(PADDRINFOA info);
