// okay-ms own <dirent.h>, written from POSIX.1-2017 (<dirent.h>): reading a directory. Declarations only (see
// <sys/socket.h>); DIR is an incomplete type, as the standard allows. d_name's size is our own choice (Round 35).
#pragma once
#include <sys/types.h>
typedef struct okay_dir__ DIR;
struct dirent {
    ino_t d_ino;
    char d_name[256];
};
extern "C" {
DIR* opendir(const char* path);
struct dirent* readdir(DIR* dir);
int closedir(DIR* dir);
void rewinddir(DIR* dir);
}
