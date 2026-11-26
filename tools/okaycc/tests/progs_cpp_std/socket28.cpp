// Round 28: <sys/socket.h>, <netinet/in.h>, <arpa/inet.h>, <netdb.h>, <sys/select.h> and lib/net.c
// (engine/src/Net/UdpSocket.cpp, Net/ServerQuery.cpp): two UDP sockets on the loopback address send
// a datagram to each other; byte order, address text and getaddrinfo for numbers and localhost.
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

int main() {
    std::printf("%04x %08x %04x\n", (unsigned)htons(0x1234), (unsigned)htonl(0x01020304u), (unsigned)ntohs(htons(80)));
    in_addr a;
    int ok = inet_pton(AF_INET, "192.168.1.20", &a);
    char text[INET_ADDRSTRLEN];
    std::printf("%d %s %08x\n", ok, inet_ntop(AF_INET, &a, text, sizeof text), (unsigned)ntohl(a.s_addr));
    std::printf("%d %d\n", inet_pton(AF_INET, "300.1.1.1", &a), inet_pton(AF_INET, "1.2.3", &a));

    addrinfo hints;
    std::memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    const char* names[] = {"localhost", "10.0.0.7"};
    for (const char* n : names) {
        addrinfo* res = nullptr;
        int rc = getaddrinfo(n, "9000", &hints, &res);
        if (rc != 0 || !res) {
            std::printf("%s: error\n", n);
            continue;
        }
        sockaddr_in* sin = (sockaddr_in*)res->ai_addr;
        std::printf("%s: %s %d %d %d\n", n, inet_ntop(AF_INET, &sin->sin_addr, text, sizeof text), ntohs(sin->sin_port),
                    res->ai_socktype, res->ai_family);
        freeaddrinfo(res);
    }

    int s1 = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    int s2 = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    int yes = 1;
    std::printf("sockets %d %d %d\n", s1 >= 0, s2 >= 0, setsockopt(s1, SOL_SOCKET, SO_BROADCAST, &yes, sizeof yes));
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    std::printf("bind %d %d\n", bind(s1, (sockaddr*)&addr, sizeof addr), bind(s2, (sockaddr*)&addr, sizeof addr));
    sockaddr_in got;
    socklen_t len = sizeof got;
    getsockname(s2, (sockaddr*)&got, &len);
    std::printf("name %d %s %d\n", (int)len, inet_ntop(AF_INET, &got.sin_addr, text, sizeof text), got.sin_port != 0);

    int fl = fcntl(s2, F_GETFL, 0);
    fcntl(s2, F_SETFL, fl | O_NONBLOCK);
    char buf[64];
    long none = recv(s2, buf, sizeof buf, 0);
    std::printf("empty %ld %d\n", none, errno == EAGAIN || errno == EWOULDBLOCK);

    const char msg[] = "hello over udp";
    long sent = sendto(s1, msg, sizeof msg, 0, (sockaddr*)&got, sizeof got);
    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(s2, &rd);
    timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    int ready = select(s2 + 1, &rd, nullptr, nullptr, &tv);
    sockaddr_in from;
    socklen_t flen = sizeof from;
    long n = recvfrom(s2, buf, sizeof buf, 0, (sockaddr*)&from, &flen);
    std::printf("sent %ld ready %d isset %d got %ld '%s' from %s %d\n", sent, ready, (int)FD_ISSET(s2, &rd) != 0, n,
                n > 0 ? buf : "", inet_ntop(AF_INET, &from.sin_addr, text, sizeof text), (int)flen);
    close(s1);
    close(s2);
    return 0;
}
