/* Whole-file reading and writing for okay-ms programs (the engine of <fstream>), on kernel32. Written from the Win32
   documentation of CreateFileA, GetFileSize, ReadFile, WriteFile and CloseHandle. */
__declspec(dllimport) void *CreateFileA(const char *name, unsigned long access, unsigned long share, void *security,
                                        unsigned long disposition, unsigned long flags, void *templ);
__declspec(dllimport) unsigned long GetFileSize(void *file, unsigned long *high);
__declspec(dllimport) int ReadFile(void *file, void *buffer, unsigned long count, unsigned long *done, void *overlapped);
__declspec(dllimport) int WriteFile(void *file, const void *buffer, unsigned long count, unsigned long *done, void *overlapped);
__declspec(dllimport) int CloseHandle(void *file);
void *malloc(unsigned long long n);

/* Reads the file into a new block (free it with free); returns its size, or -1 when it cannot be opened. */
long long okay_read_file(const char *path, char **data) {
    void *f = CreateFileA(path, 0x80000000ul, 1, 0, 3, 0x80, 0); /* GENERIC_READ, share read, OPEN_EXISTING */
    unsigned long size;
    unsigned long done = 0;
    char *block;
    if (f == (void *)(long long)-1) return -1;
    size = GetFileSize(f, 0);
    if (size == 0xFFFFFFFFul) {
        CloseHandle(f);
        return -1;
    }
    block = (char *)malloc((unsigned long long)size + 1);
    if (block == 0) {
        CloseHandle(f);
        return -1;
    }
    if (size > 0) ReadFile(f, block, size, &done, 0);
    block[done] = 0;
    CloseHandle(f);
    *data = block;
    return (long long)done;
}

/* Writes the bytes to the file (replacing it, or appending when append is not 0); 1 on success, 0 on failure. */
int okay_write_file(const char *path, const char *data, long long size, int append) {
    void *f;
    unsigned long done = 0;
    f = CreateFileA(path, append ? 0x4ul : 0x40000000ul, 0, 0, append ? 4ul : 2ul, 0x80, 0);
    if (f == (void *)(long long)-1) return 0;
    if (size > 0) WriteFile(f, data, (unsigned long)size, &done, 0);
    CloseHandle(f);
    return done == (unsigned long)size;
}
