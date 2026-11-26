// okay-ms own <sys/wait.h>, written from POSIX.1-2017 (<sys/wait.h>, waitpid). Declarations only (see <sys/types.h>):
// nothing defines waitpid on Windows. The status word is read the traditional way POSIX leaves to the implementation
// (the exit status in bits 8 to 15, a terminating signal in the low 7 bits); the macros are our own choice of that
// layout and only matter to code that runs, which needs a POSIX system anyway (Round 30).
#pragma once
#include <sys/types.h>
#define WNOHANG 1
#define WUNTRACED 2
#define WEXITSTATUS(s) (((s) >> 8) & 0xFF)
#define WTERMSIG(s) ((s) & 0x7F)
#define WIFEXITED(s) (WTERMSIG(s) == 0)
#define WIFSIGNALED(s) (WTERMSIG(s) != 0 && WTERMSIG(s) != 0x7F)
#define WIFSTOPPED(s) (((s) & 0xFF) == 0x7F)
extern "C" {
pid_t waitpid(pid_t pid, int* status, int options);
pid_t wait(int* status);
}
