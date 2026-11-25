/* Files for <cstdio> (ISO C 7.21: fopen, fclose, fread, fwrite, fgetc, fgets, fputc, fputs, fseek,
 * ftell, rewind, feof, ferror, clearerr, fflush, fprintf, remove, rename, putchar; puts is in extra.c), on the
 * Linux system calls read 0, write 1, open 2, close 3, lseek 8, rename 82 and unlink 87 (the x86-64
 * system call table). Our own code. Reads go through a 4096-byte buffer per file; writes are not
 * buffered (each fwrite is one write call), so there is nothing to lose if a program exits without
 * fclose. stdin, stdout and stderr are descriptors 0, 1 and 2. */
#if defined(__okaycc__)
#include "stdarg.h"
#else
#include <stdarg.h>
#endif
long syscall(long n, long a, long b, long c);
char *malloc(int n);
void free(char *p);
int __okay_vdprintf(int fd, char *fmt, va_list ap);
void __okay_sink_flush(void);

struct __okay_file {
    int fd;
    int eof;
    int err;
    int pos;  /* next unread byte in buf */
    int len;  /* bytes in buf */
    char buf[4096];
};
typedef struct __okay_file FILE;

static FILE gIn = {0, 0, 0, 0, 0};
static FILE gOutF = {1, 0, 0, 0, 0};
static FILE gErr = {2, 0, 0, 0, 0};
FILE *stdin = &gIn;
FILE *stdout = &gOutF;
FILE *stderr = &gErr;

FILE *fopen(char *path, char *mode) {
    long flags;
    long fd;
    FILE *f;
    int plus;
    int k;
    plus = 0;
    for (k = 1; mode[k] != 0; k = k + 1)
        if (mode[k] == '+') plus = 1;
    /* O_RDONLY 0, O_WRONLY 1, O_RDWR 2, O_CREAT 64, O_TRUNC 512, O_APPEND 1024 */
    if (mode[0] == 'r') flags = plus ? 2 : 0;
    else if (mode[0] == 'w') flags = (plus ? 2 : 1) | 64 | 512;
    else if (mode[0] == 'a') flags = (plus ? 2 : 1) | 64 | 1024;
    else return 0;
    fd = syscall(2, (long)path, flags, 420); /* mode 0644 */
    if (fd < 0) return 0;
    f = (FILE *)malloc((int)sizeof(FILE));
    if (f == 0) {
        syscall(3, fd, 0, 0);
        return 0;
    }
    f->fd = (int)fd;
    f->eof = 0;
    f->err = 0;
    f->pos = 0;
    f->len = 0;
    return f;
}

int fclose(FILE *f) {
    long r;
    if (f == 0) return -1;
    r = syscall(3, (long)f->fd, 0, 0);
    if (f != stdin && f != stdout && f != stderr) free((char *)f);
    return r < 0 ? -1 : 0;
}

static int fill(FILE *f) {
    long r;
    f->pos = 0;
    f->len = 0;
    r = syscall(0, (long)f->fd, (long)f->buf, 4096);
    if (r < 0) {
        f->err = 1;
        return 0;
    }
    if (r == 0) {
        f->eof = 1;
        return 0;
    }
    f->len = (int)r;
    return 1;
}

int fgetc(FILE *f) {
    if (f->pos >= f->len && !fill(f)) return -1;
    f->pos = f->pos + 1;
    return (int)(unsigned char)f->buf[f->pos - 1];
}
int getc(FILE *f) { return fgetc(f); }
int getchar(void) { return fgetc(stdin); }

int ungetc(int c, FILE *f) {
    if (c < 0 || f->pos == 0) return -1;
    f->pos = f->pos - 1;
    f->buf[f->pos] = (char)c;
    f->eof = 0;
    return c;
}

char *fgets(char *s, int n, FILE *f) {
    int i;
    int c;
    i = 0;
    if (n <= 0) return 0;
    while (i < n - 1) {
        c = fgetc(f);
        if (c < 0) break;
        s[i] = (char)c;
        i = i + 1;
        if (c == '\n') break;
    }
    if (i == 0) return 0;
    s[i] = 0;
    return s;
}

unsigned long fread(char *p, unsigned long size, unsigned long count, FILE *f) {
    unsigned long want;
    unsigned long got;
    long r;
    if (size == 0 || count == 0) return 0;
    want = size * count;
    got = 0;
    while (got < want && f->pos < f->len) {
        p[got] = f->buf[f->pos];
        f->pos = f->pos + 1;
        got = got + 1;
    }
    while (got < want) {
        r = syscall(0, (long)f->fd, (long)(p + got), (long)(want - got));
        if (r < 0) {
            f->err = 1;
            break;
        }
        if (r == 0) {
            f->eof = 1;
            break;
        }
        got = got + (unsigned long)r;
    }
    return got / size;
}

/* A write discards what is buffered for reading: the descriptor is moved back to the logical position. */
static void dropReadBuffer(FILE *f) {
    if (f->pos < f->len) syscall(8, (long)f->fd, (long)(f->pos - f->len), 1);
    f->pos = 0;
    f->len = 0;
}

unsigned long fwrite(char *p, unsigned long size, unsigned long count, FILE *f) {
    unsigned long want;
    unsigned long done;
    long r;
    if (size == 0 || count == 0) return 0;
    if (f == stdout) __okay_sink_flush();
    dropReadBuffer(f);
    want = size * count;
    done = 0;
    while (done < want) {
        r = syscall(1, (long)f->fd, (long)(p + done), (long)(want - done));
        if (r <= 0) {
            f->err = 1;
            break;
        }
        done = done + (unsigned long)r;
    }
    return done / size;
}

int fputc(int c, FILE *f) {
    char b;
    b = (char)c;
    if (fwrite(&b, 1, 1, f) != 1) return -1;
    return (int)(unsigned char)b;
}
int putc(int c, FILE *f) { return fputc(c, f); }
int putchar(int c) { return fputc(c, stdout); }

int fputs(char *s, FILE *f) {
    unsigned long n;
    n = 0;
    while (s[n] != 0) n = n + 1;
    if (fwrite(s, 1, n, f) != n) return -1;
    return 1;
}

int fflush(FILE *f) {
    (void)f;
    __okay_sink_flush();
    return 0;
}

int fseek(FILE *f, long off, int whence) {
    long r;
    if (whence == 1) off = off - (long)(f->len - f->pos); /* SEEK_CUR from the logical position */
    f->pos = 0;
    f->len = 0;
    r = syscall(8, (long)f->fd, off, (long)whence);
    if (r < 0) return -1;
    f->eof = 0;
    return 0;
}

long ftell(FILE *f) {
    long r;
    r = syscall(8, (long)f->fd, 0, 1);
    if (r < 0) return -1;
    return r - (long)(f->len - f->pos);
}

void rewind(FILE *f) {
    fseek(f, 0, 0);
    f->err = 0;
}
int feof(FILE *f) { return f->eof; }
int ferror(FILE *f) { return f->err; }
void clearerr(FILE *f) {
    f->eof = 0;
    f->err = 0;
}
int fileno(FILE *f) { return f->fd; }

int vfprintf(FILE *f, char *fmt, va_list ap) {
    dropReadBuffer(f);
    return __okay_vdprintf(f->fd, fmt, ap);
}
int fprintf(FILE *f, char *fmt, ...) {
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int remove(char *path) { return syscall(87, (long)path, 0, 0) < 0 ? -1 : 0; }
int rename(char *from, char *to) { return syscall(82, (long)from, (long)to, 0) < 0 ? -1 : 0; }

/* POSIX file descriptors (<fcntl.h>, <unistd.h>): open 2, close 3, read 0, lseek 8, fcntl 72,
 * fsync 74, ftruncate 77, unlink 87, getpid 39, nanosleep 35, access 21 (the x86-64 Linux system
 * call table). A failed call returns -1 and sets errno to the error number the kernel gave. */
int *__okay_errno(void);
static long sysResult(long r) {
    if (r < 0 && r > -4096) {
        *__okay_errno() = (int)-r;
        return -1;
    }
    return r;
}
int open(char *path, int flags, ...) {
    va_list ap;
    int mode = 0;
    if (flags & 64) { /* O_CREAT: the third argument is the mode */
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    return (int)sysResult(syscall(2, (long)path, (long)flags, (long)mode));
}
int creat(char *path, int mode) { return open(path, 64 | 1 | 512, mode); }
int close(int fd) { return (int)sysResult(syscall(3, (long)fd, 0, 0)); }
long read(int fd, char *buf, long n) { return sysResult(syscall(0, (long)fd, (long)buf, n)); }
long lseek(int fd, long off, int whence) { return sysResult(syscall(8, (long)fd, off, (long)whence)); }
int fcntl(int fd, int cmd, ...) {
    va_list ap;
    long arg;
    va_start(ap, cmd);
    arg = va_arg(ap, long);
    va_end(ap);
    return (int)sysResult(syscall(72, (long)fd, (long)cmd, arg));
}
int fsync(int fd) { return (int)sysResult(syscall(74, (long)fd, 0, 0)); }
int ftruncate(int fd, long len) { return (int)sysResult(syscall(77, (long)fd, len, 0)); }
int unlink(char *path) { return (int)sysResult(syscall(87, (long)path, 0, 0)); }
int access(char *path, int mode) { return (int)sysResult(syscall(21, (long)path, (long)mode, 0)); }
int getpid(void) { return (int)syscall(39, 0, 0, 0); }
struct __okay_timespec {
    long sec;
    long nsec;
};
int nanosleep(struct __okay_timespec *req, struct __okay_timespec *rem) {
    return (int)sysResult(syscall(35, (long)req, (long)rem, 0));
}
int usleep(unsigned int us) {
    struct __okay_timespec t;
    t.sec = us / 1000000;
    t.nsec = (long)(us % 1000000) * 1000;
    return nanosleep(&t, 0);
}
unsigned int sleep(unsigned int s) {
    struct __okay_timespec t;
    t.sec = s;
    t.nsec = 0;
    nanosleep(&t, 0);
    return 0;
}
