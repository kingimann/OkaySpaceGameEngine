/* <time.h> for okay-cc (ISO C 7.27, POSIX <time.h>): the same as <ctime>, and POSIX nanosleep
 * (lib/stdio.c, the Linux x86-64 system call 35). Our own header. */
#pragma once
#include <ctime>
extern "C" int nanosleep(struct timespec *req, struct timespec *rem);
