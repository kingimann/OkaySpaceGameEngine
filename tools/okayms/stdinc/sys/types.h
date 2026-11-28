// okay-ms own <sys/types.h>, written from POSIX.1-2017 (The Open Group Base Specifications Issue 7, <sys/types.h>).
// The engine's POSIX paths (the plain survey, where _WIN32 is not defined) include it. okay-ms targets Windows: the
// types are declared so that those paths compile; the Microsoft C runtime has no POSIX process functions, so a program
// that calls one does not link (Round 30).
#pragma once
typedef int pid_t;
typedef long long ssize_t;
typedef unsigned int mode_t;
typedef long long off_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef unsigned long long dev_t;
typedef unsigned long long ino_t;
typedef unsigned long long nlink_t;
typedef long long blksize_t;
typedef long long blkcnt_t;
typedef unsigned int useconds_t;  // Round 35: ServerQuery.cpp's usleep((useconds_t)ms * 1000)
