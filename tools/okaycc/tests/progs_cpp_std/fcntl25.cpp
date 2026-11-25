// <fcntl.h> and <unistd.h> (POSIX): open with a mode, write, lseek, read, fcntl flags, unlink, and
// errno after a failed open.
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
int main() {
    const char *path = "/tmp/okaycc_fcntl25.txt";
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    std::printf("%d\n", fd >= 0);
    const char *msg = "hello fcntl";
    std::printf("%ld\n", (long)write(fd, msg, std::strlen(msg)));
    std::printf("%d\n", close(fd));
    fd = open(path, O_RDONLY);
    char buf[32] = {0};
    long n = (long)read(fd, buf, 5);
    std::printf("%ld %s\n", n, buf);
    std::printf("%ld\n", (long)lseek(fd, 0, SEEK_END));
    int fl = fcntl(fd, F_GETFL);
    std::printf("%d\n", (fl & O_ACCMODE) == O_RDONLY);
    std::printf("%d\n", fcntl(fd, F_SETFD, FD_CLOEXEC) == 0 && (fcntl(fd, F_GETFD) & FD_CLOEXEC) != 0);
    close(fd);
    int there = access(path, F_OK);
    int gone = unlink(path);
    std::printf("%d %d %d\n", there, gone, access(path, F_OK));
    int bad = open("/tmp/okaycc_no_such_dir_25/x", O_RDONLY);
    std::printf("%d %d\n", bad, errno == ENOENT);
}
