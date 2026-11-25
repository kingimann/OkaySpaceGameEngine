/* <sys/types.h> for okay-cc (POSIX): the Linux x86-64 sizes. Our own header. */
#pragma once
typedef long ssize_t;
typedef long off_t;
typedef unsigned int mode_t;
typedef int pid_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
#ifndef __okay_size_t_defined
#define __okay_size_t_defined
typedef unsigned long size_t;
#endif
