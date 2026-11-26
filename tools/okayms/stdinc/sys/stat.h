// okay-ms own <sys/stat.h>, written from POSIX.1-2017 (<sys/stat.h>). Declarations only (see <sys/types.h>). The mode
// bits are the values POSIX gives for the permission bits (S_IRUSR 0400 ... S_IXOTH 01); the file-type bits are our own
// choice of the traditional layout (Round 30).
#pragma once
#include <sys/types.h>
#define S_IFMT 0170000
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_IFLNK 0120000
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#define S_IRWXU 0700
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRWXG 070
#define S_IRGRP 040
#define S_IWGRP 020
#define S_IXGRP 010
#define S_IRWXO 07
#define S_IROTH 04
#define S_IWOTH 02
#define S_IXOTH 01
struct stat {
    dev_t st_dev;
    ino_t st_ino;
    mode_t st_mode;
    nlink_t st_nlink;
    uid_t st_uid;
    gid_t st_gid;
    off_t st_size;
    long long st_atime;
    long long st_mtime;
    long long st_ctime;
};
extern "C" {
int stat(const char* path, struct stat* buf);
int fstat(int fd, struct stat* buf);
int lstat(const char* path, struct stat* buf);
int chmod(const char* path, mode_t mode);
int mkdir(const char* path, mode_t mode);
mode_t umask(mode_t mask);
}
