// okay-ms own <spawn.h>, written from POSIX.1-2017 (<spawn.h>, posix_spawn). Declarations only (see <sys/types.h>):
// the engine's POSIX path of starting a program (CurlFallback.hpp, AppRestart.hpp) compiles; nothing defines these on
// Windows. The two object types are opaque here: a block of storage the functions would fill (Round 30).
#pragma once
#include <sys/types.h>
struct sched_param;
typedef struct {
    void* __okay_state[8];
} posix_spawn_file_actions_t;
typedef struct {
    void* __okay_state[8];
} posix_spawnattr_t;
extern "C" {
int posix_spawn(pid_t* pid, const char* path, const posix_spawn_file_actions_t* actions,
                const posix_spawnattr_t* attr, char* const argv[], char* const envp[]);
int posix_spawnp(pid_t* pid, const char* file, const posix_spawn_file_actions_t* actions,
                 const posix_spawnattr_t* attr, char* const argv[], char* const envp[]);
int posix_spawn_file_actions_init(posix_spawn_file_actions_t* actions);
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t* actions);
int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t* actions, int fd, const char* path, int oflag,
                                     mode_t mode);
int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t* actions, int fd);
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t* actions, int fd, int newfd);
int posix_spawnattr_init(posix_spawnattr_t* attr);
int posix_spawnattr_destroy(posix_spawnattr_t* attr);
int posix_spawnattr_setflags(posix_spawnattr_t* attr, short flags);
}
#define POSIX_SPAWN_SETPGROUP 2
#define POSIX_SPAWN_SETSIGMASK 8
