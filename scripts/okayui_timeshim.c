// LD_PRELOAD shim for scripts/okayui_hermetic_check.sh: moves time(), gettimeofday() and the realtime clock by FAKE_OFFSET seconds, so a golden test can be run "on another day".
// Build: gcc -shared -fPIC -O1 scripts/okayui_timeshim.c -o timeshim.so -ldl
#define _GNU_SOURCE
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <dlfcn.h>
static long off(void){ const char* e=getenv("FAKE_OFFSET"); return e?atol(e):0; }
time_t time(time_t* t){ static time_t(*real)(time_t*); if(!real) real=dlsym(RTLD_NEXT,"time"); time_t v=real(0)+off(); if(t)*t=v; return v; }
int gettimeofday(struct timeval* tv, void* tz){ static int(*real)(struct timeval*,void*); if(!real) real=dlsym(RTLD_NEXT,"gettimeofday"); int r=real(tv,tz); if(tv) tv->tv_sec+=off(); return r; }
int clock_gettime(clockid_t c, struct timespec* ts){ static int(*real)(clockid_t,struct timespec*); if(!real) real=dlsym(RTLD_NEXT,"clock_gettime"); int r=real(c,ts); if(ts && (c==CLOCK_REALTIME)) ts->tv_sec+=off(); return r; }
