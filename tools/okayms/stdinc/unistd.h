// okay-ms own <unistd.h>, written from POSIX.1-2017 (<unistd.h>). Declarations only (see <sys/types.h>): the Microsoft
// C runtime has none of these under these names, so they compile for the engine's POSIX paths and do not link (Round 30).
#pragma once
#include <sys/types.h>
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
extern "C" {
pid_t fork(void);
pid_t getpid(void);
pid_t getppid(void);
pid_t setsid(void);
int close(int fd);
ssize_t read(int fd, void* buf, unsigned long long n);
ssize_t write(int fd, const void* buf, unsigned long long n);
int pipe(int fds[2]);
int dup(int fd);
int dup2(int fd, int fd2);
int execv(const char* path, char* const argv[]);
int execve(const char* path, char* const argv[], char* const envp[]);
int execvp(const char* file, char* const argv[]);
int chdir(const char* path);
char* getcwd(char* buf, unsigned long long size);
ssize_t readlink(const char* path, char* buf, unsigned long long size);
int unlink(const char* path);
int rmdir(const char* path);
int access(const char* path, int mode);
int isatty(int fd);
unsigned sleep(unsigned seconds);
int usleep(unsigned long long usec);
off_t lseek(int fd, off_t offset, int whence);
int fsync(int fd);
int ftruncate(int fd, off_t length);
long sysconf(int name);
uid_t getuid(void);
uid_t geteuid(void);
void _exit(int status);
}
