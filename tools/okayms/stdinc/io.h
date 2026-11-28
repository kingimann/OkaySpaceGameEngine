// okay-ms own header <io.h>: the low-level file functions of the Microsoft C runtime, declarations only, written from
// Microsoft's public C runtime reference (the pages of _open, _close, _read, _write, _lseek, _commit, _access, _dup,
// _isatty, _setmode, _get_osfhandle and the others named here: their documented prototypes, "Required header <io.h>").
// The runtime DLLs export these names (ucrtbase.dll, msvcrt.dll); okay-ms's own runtime does not define them, so a
// program that calls one links only against a library that does. The open flags are in <fcntl.h>. Round 36
// (SaveFile.cpp's `_commit(_fileno(f))`).
#pragma once
extern "C" {
int _access(const char *path, int mode);
int _waccess(const unsigned short *path, int mode);
int _chmod(const char *path, int mode);
int _close(int fd);
int _commit(int fd);
int _creat(const char *path, int mode);
int _dup(int fd);
int _dup2(int fd1, int fd2);
int _eof(int fd);
long _filelength(int fd);
long long _filelengthi64(int fd);
int _isatty(int fd);
int _locking(int fd, int mode, long nbytes);
long _lseek(int fd, long offset, int origin);
long long _lseeki64(int fd, long long offset, int origin);
int _open(const char *path, int oflag, ...);
int _wopen(const unsigned short *path, int oflag, ...);
int _read(int fd, void *buffer, unsigned int count);
int _write(int fd, const void *buffer, unsigned int count);
int _setmode(int fd, int mode);
int _sopen(const char *path, int oflag, int shflag, ...);
long _tell(int fd);
long long _telli64(int fd);
int _umask(int mode);
int _unlink(const char *path);
int _wunlink(const unsigned short *path);
long long _get_osfhandle(int fd);
int _open_osfhandle(long long handle, int flags);
}
