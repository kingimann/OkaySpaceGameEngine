// okay-ms own header <fcntl.h>: the file-open flags of the Microsoft C runtime, written from Microsoft's documentation
// of _open ("oflag" constants) and of the POSIX names it keeps beside the underscore names. Flags only, as in that
// runtime: _open and the other low-level file functions are declared in <io.h> there (not provided here yet).
#pragma once
#define _O_RDONLY 0x0000
#define _O_WRONLY 0x0001
#define _O_RDWR 0x0002
#define _O_APPEND 0x0008
#define _O_CREAT 0x0100
#define _O_TRUNC 0x0200
#define _O_EXCL 0x0400
#define _O_TEXT 0x4000
#define _O_BINARY 0x8000
#define O_RDONLY _O_RDONLY
#define O_WRONLY _O_WRONLY
#define O_RDWR _O_RDWR
#define O_APPEND _O_APPEND
#define O_CREAT _O_CREAT
#define O_TRUNC _O_TRUNC
#define O_EXCL _O_EXCL
#define O_TEXT _O_TEXT
#define O_BINARY _O_BINARY
// POSIX.1-2017 <fcntl.h> beyond the flags (Round 30): open and fcntl and the descriptor commands, for the engine's POSIX
// paths (the plain survey). Declared only when _WIN32 is not defined; the Microsoft runtime has no fcntl, and the
// command values are our own choice (nothing that calls them links on Windows).
#if !defined(_WIN32)
#include <sys/types.h>
#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define FD_CLOEXEC 1
#define O_NONBLOCK 0x10000
#define O_CLOEXEC 0x20000
extern "C" {
int open(const char* path, int oflag, ...);
int fcntl(int fd, int cmd, ...);
}
#endif
