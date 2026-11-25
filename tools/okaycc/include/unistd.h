/* <unistd.h> for okay-cc (POSIX <unistd.h>): the file-descriptor calls in lib/write.c and
 * lib/stdio.c, on the Linux system calls. Our own header. */
#pragma once
#include <sys/types.h>
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
#ifdef __cplusplus
extern "C" {
#endif
ssize_t read(int fd, void *buf, size_t n);
ssize_t write(int fd, const void *buf, size_t n);
int close(int fd);
off_t lseek(int fd, off_t off, int whence);
int fsync(int fd);
int ftruncate(int fd, off_t len);
int unlink(const char *path);
int access(const char *path, int mode);
pid_t getpid(void);
int usleep(unsigned int us);
unsigned int sleep(unsigned int s);
#ifdef __cplusplus
}
#endif
