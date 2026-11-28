// okay-ms own <dlfcn.h>, written from POSIX.1-2017 (<dlfcn.h>): loading shared objects at run time. Declarations only
// (see <sys/socket.h>): on Windows the same work is LoadLibrary and GetProcAddress (<windows.h>). The flag numbers are our
// own choice (Round 35; OwnLinuxAudio.cpp).
#pragma once
#define RTLD_LAZY 0x001
#define RTLD_NOW 0x002
#define RTLD_GLOBAL 0x100
#define RTLD_LOCAL 0
extern "C" {
void* dlopen(const char* file, int mode);
void* dlsym(void* handle, const char* name);
int dlclose(void* handle);
char* dlerror(void);
}
