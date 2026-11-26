/* Sockets for okay-cc programs (POSIX <sys/socket.h>, <arpa/inet.h>, <netdb.h>, <sys/select.h>),
 * written from the Linux man pages socket(2), bind(2), connect(2), sendmsg(2), recvmsg(2),
 * setsockopt(2), select(2), inet_pton(3), getaddrinfo(3) and hosts(5), on the x86-64 system calls.
 * send, recv, sendto and recvfrom go through sendmsg and recvmsg, which take three arguments.
 * getaddrinfo knows numeric IPv4 addresses, "localhost" and /etc/hosts; it asks no DNS server.
 * Our own code. */
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
long syscall(long n, long a, long b, long c, long d, long e);
int *__okay_errno(void);
void *malloc(unsigned long n);
void free(void *p);
int open(const char *path, int flags, ...);
long read(int fd, void *buf, unsigned long n);
int close(int fd);

static long __okay_net_fail(long r) {
    if (r < 0 && r > -4096) {
        *__okay_errno() = (int)-r;
        return -1;
    }
    return r;
}

/* the kernel's struct msghdr and struct iovec (x86-64: 56 and 16 bytes) */
struct __okay_iov {
    void *base;
    unsigned long len;
};
struct __okay_msghdr {
    void *name;
    unsigned int namelen;
    struct __okay_iov *iov;
    unsigned long iovlen;
    void *control;
    unsigned long controllen;
    int flags;
};

int socket(int domain, int type, int protocol) { return (int)__okay_net_fail(syscall(41, domain, type, protocol, 0, 0)); }
int bind(int fd, const struct sockaddr *addr, socklen_t len) {
    return (int)__okay_net_fail(syscall(49, fd, (long)addr, len, 0, 0));
}
int connect(int fd, const struct sockaddr *addr, socklen_t len) {
    return (int)__okay_net_fail(syscall(42, fd, (long)addr, len, 0, 0));
}
int listen(int fd, int backlog) { return (int)__okay_net_fail(syscall(50, fd, backlog, 0, 0, 0)); }
int accept(int fd, struct sockaddr *addr, socklen_t *len) {
    return (int)__okay_net_fail(syscall(43, fd, (long)addr, (long)len, 0, 0));
}
ssize_t sendto(int fd, const void *buf, size_t n, int flags, const struct sockaddr *to, socklen_t len) {
    struct __okay_iov v;
    struct __okay_msghdr m;
    v.base = (void *)buf;
    v.len = n;
    m.name = (void *)to;
    m.namelen = to ? len : 0;
    m.iov = &v;
    m.iovlen = 1;
    m.control = 0;
    m.controllen = 0;
    m.flags = 0;
    return __okay_net_fail(syscall(46, fd, (long)&m, flags, 0, 0));
}
ssize_t recvfrom(int fd, void *buf, size_t n, int flags, struct sockaddr *from, socklen_t *len) {
    struct __okay_iov v;
    struct __okay_msghdr m;
    long r;
    v.base = buf;
    v.len = n;
    m.name = from;
    m.namelen = from && len ? *len : 0;
    m.iov = &v;
    m.iovlen = 1;
    m.control = 0;
    m.controllen = 0;
    m.flags = 0;
    r = __okay_net_fail(syscall(47, fd, (long)&m, flags, 0, 0));
    if (r >= 0 && from && len) *len = m.namelen;
    return r;
}
ssize_t send(int fd, const void *buf, size_t n, int flags) { return sendto(fd, buf, n, flags, 0, 0); }
ssize_t recv(int fd, void *buf, size_t n, int flags) { return recvfrom(fd, buf, n, flags, 0, 0); }
int setsockopt(int fd, int level, int name, const void *value, socklen_t len) {
    return (int)__okay_net_fail(syscall(54, fd, level, name, (long)value, len));
}
int getsockopt(int fd, int level, int name, void *value, socklen_t *len) {
    return (int)__okay_net_fail(syscall(55, fd, level, name, (long)value, (long)len));
}
int getsockname(int fd, struct sockaddr *addr, socklen_t *len) {
    return (int)__okay_net_fail(syscall(51, fd, (long)addr, (long)len, 0, 0));
}
int getpeername(int fd, struct sockaddr *addr, socklen_t *len) {
    return (int)__okay_net_fail(syscall(52, fd, (long)addr, (long)len, 0, 0));
}
int shutdown(int fd, int how) { return (int)__okay_net_fail(syscall(48, fd, how, 0, 0, 0)); }

void __okay_fd_zero(fd_set *s) {
    int i;
    for (i = 0; i < 16; ++i) s->fds_bits[i] = 0;
}
int select(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *timeout) {
    return (int)__okay_net_fail(syscall(23, n, (long)r, (long)w, (long)e, (long)timeout));
}

/* Byte order: the network's is big-endian, x86-64's little-endian. */
unsigned short htons(unsigned short v) { return (unsigned short)((v >> 8) | (v << 8)); }
unsigned short ntohs(unsigned short v) { return htons(v); }
unsigned int htonl(unsigned int v) {
    return (v >> 24) | ((v >> 8) & 0xff00u) | ((v << 8) & 0xff0000u) | (v << 24);
}
unsigned int ntohl(unsigned int v) { return htonl(v); }

/* "a.b.c.d", each part a decimal 0..255 with no sign, exactly four parts (inet_pton(3) for AF_INET).
 * The address is written in network byte order. Returns 1, or 0 for text that is not one. */
static int __okay_parse_v4(const char *s, unsigned char out[4]) {
    int part = 0;
    while (part < 4) {
        int v = 0, digits = 0;
        while (*s >= '0' && *s <= '9') {
            v = v * 10 + (*s - '0');
            ++s;
            if (++digits > 3 || v > 255) return 0;
        }
        if (digits == 0) return 0;
        out[part++] = (unsigned char)v;
        if (part < 4) {
            if (*s != '.') return 0;
            ++s;
        }
    }
    return *s == 0;
}
int inet_pton(int family, const char *text, void *out) {
    if (family != AF_INET) {
        *__okay_errno() = 97; /* EAFNOSUPPORT: IPv6 text is not done */
        return -1;
    }
    return __okay_parse_v4(text, (unsigned char *)out);
}
in_addr_t inet_addr(const char *text) {
    in_addr_t a;
    if (!__okay_parse_v4(text, (unsigned char *)&a)) return INADDR_NONE;
    return a;
}
const char *inet_ntop(int family, const void *addr, char *out, socklen_t size) {
    const unsigned char *b = (const unsigned char *)addr;
    char tmp[16];
    int n = 0, i;
    if (family != AF_INET) {
        *__okay_errno() = 97;
        return 0;
    }
    for (i = 0; i < 4; ++i) {
        int v = b[i];
        if (i) tmp[n++] = '.';
        if (v >= 100) tmp[n++] = (char)('0' + v / 100);
        if (v >= 10) tmp[n++] = (char)('0' + v / 10 % 10);
        tmp[n++] = (char)('0' + v % 10);
    }
    tmp[n++] = 0;
    if ((socklen_t)n > size) {
        *__okay_errno() = 28; /* ENOSPC */
        return 0;
    }
    for (i = 0; i < n; ++i) out[i] = tmp[i];
    return out;
}
char *inet_ntoa(struct in_addr a) {
    static char buf[16];
    inet_ntop(AF_INET, &a, buf, 16);
    return buf;
}

/* /etc/hosts (hosts(5)): "address name [aliases...]" lines, '#' starts a comment. The first IPv4
 * line that names 'name' (letters compared without case) gives the address. */
static int __okay_same_name(const char *a, const char *b, int n) {
    int i;
    for (i = 0; i < n; ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y || !y) return 0;
    }
    return b[n] == 0;
}
static int __okay_hosts_lookup(const char *name, unsigned char out[4]) {
    static char buf[16384];
    long len = 0, r;
    int fd = open("/etc/hosts", 0), found = 0;
    long i = 0;
    if (fd < 0) return 0;
    while (len < (long)sizeof buf - 1 && (r = read(fd, buf + len, sizeof buf - 1 - len)) > 0) len += r;
    close(fd);
    buf[len] = 0;
    while (i < len && !found) {
        long end = i, w;
        char addr[64];
        int an = 0, first = 1;
        while (end < len && buf[end] != '\n') ++end;
        for (w = i; w < end && buf[w] != '#';) {
            long s;
            while (w < end && (buf[w] == ' ' || buf[w] == '\t' || buf[w] == '\r')) ++w;
            s = w;
            while (w < end && buf[w] != ' ' && buf[w] != '\t' && buf[w] != '\r' && buf[w] != '#') ++w;
            if (w == s) break;
            if (first) {
                an = (int)(w - s < 63 ? w - s : 63);
                {
                    int k;
                    for (k = 0; k < an; ++k) addr[k] = buf[s + k];
                }
                addr[an] = 0;
                first = 0;
            } else if (__okay_same_name(buf + s, name, (int)(w - s)) && __okay_parse_v4(addr, out)) {
                found = 1;
                break;
            }
        }
        i = end + 1;
    }
    return found;
}

int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res) {
    unsigned char a[4];
    int port = 0, family = hints ? hints->ai_family : AF_UNSPEC, flags = hints ? hints->ai_flags : 0;
    int types[2], ntypes = 0, k;
    struct addrinfo *head = 0, **tail = &head;
    *res = 0;
    if (!node && !service) return EAI_NONAME;
    if (family != AF_UNSPEC && family != AF_INET) return EAI_FAMILY;
    if (service) {
        const char *p = service;
        if (!*p) return EAI_SERVICE;
        while (*p) {
            if (*p < '0' || *p > '9' || port > 65535) return EAI_SERVICE; /* service names are not done */
            port = port * 10 + (*p++ - '0');
        }
        if (port > 65535) return EAI_SERVICE;
    }
    if (!node) {
        unsigned int v = (flags & AI_PASSIVE) ? INADDR_ANY : htonl(INADDR_LOOPBACK);
        a[0] = (unsigned char)v;
        a[1] = (unsigned char)(v >> 8);
        a[2] = (unsigned char)(v >> 16);
        a[3] = (unsigned char)(v >> 24);
    } else if (!__okay_parse_v4(node, a)) {
        if (flags & AI_NUMERICHOST) return EAI_NONAME;
        if (__okay_same_name("localhost", node, 9)) {
            a[0] = 127;
            a[1] = a[2] = 0;
            a[3] = 1;
        } else if (!__okay_hosts_lookup(node, a)) {
            return EAI_NONAME; /* a DNS query is not done */
        }
    }
    /* one result per socket type asked for (both, when no type is given) */
    if (!hints || hints->ai_socktype == 0) {
        types[ntypes++] = SOCK_STREAM;
        types[ntypes++] = SOCK_DGRAM;
    } else {
        types[ntypes++] = hints->ai_socktype;
    }
    for (k = 0; k < ntypes; ++k) {
        struct addrinfo *ai = (struct addrinfo *)malloc(sizeof(struct addrinfo) + sizeof(struct sockaddr_in));
        struct sockaddr_in *sin;
        unsigned char *z;
        unsigned long i;
        if (!ai) {
            freeaddrinfo(head);
            return EAI_MEMORY;
        }
        z = (unsigned char *)ai;
        for (i = 0; i < sizeof(struct addrinfo) + sizeof(struct sockaddr_in); ++i) z[i] = 0;
        sin = (struct sockaddr_in *)(ai + 1);
        sin->sin_family = AF_INET;
        sin->sin_port = htons((unsigned short)port);
        z = (unsigned char *)&sin->sin_addr;
        for (i = 0; i < 4; ++i) z[i] = a[i];
        ai->ai_family = AF_INET;
        ai->ai_socktype = types[k];
        ai->ai_protocol = hints && hints->ai_protocol ? hints->ai_protocol
                                                      : (types[k] == SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP);
        ai->ai_addrlen = sizeof(struct sockaddr_in);
        ai->ai_addr = (struct sockaddr *)sin;
        *tail = ai;
        tail = &ai->ai_next;
    }
    *res = head;
    return 0;
}
void freeaddrinfo(struct addrinfo *ai) {
    while (ai) {
        struct addrinfo *next = ai->ai_next;
        free(ai);
        ai = next;
    }
}
const char *gai_strerror(int code) {
    switch (code) {
    case EAI_NONAME: return "Name or service not known";
    case EAI_AGAIN: return "Temporary failure in name resolution";
    case EAI_FAIL: return "Non-recoverable failure in name resolution";
    case EAI_FAMILY: return "ai_family not supported";
    case EAI_SOCKTYPE: return "ai_socktype not supported";
    case EAI_SERVICE: return "Servname not supported for ai_socktype";
    case EAI_MEMORY: return "Memory allocation failure";
    case EAI_SYSTEM: return "System error";
    case EAI_BADFLAGS: return "Bad value for ai_flags";
    }
    return "Unknown error";
}
