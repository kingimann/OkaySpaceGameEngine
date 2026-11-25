// Round 23: our own <cerrno> (29 engine files stopped at it): errno can be set, read and compared
// with the error numbers.
#include <cerrno>
#include <cstdio>
static int fail(int code) {
    errno = code;
    return -1;
}
int main() {
    errno = 0;
    std::printf("%d\n", errno);
    if (fail(EAGAIN) < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) std::printf("again %d\n", errno);
    fail(ERANGE);
    std::printf("range %d %d %d\n", (int)(errno == ERANGE), EINTR, ENOENT);
    return 0;
}
