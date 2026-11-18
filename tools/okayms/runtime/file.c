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

/* ---- C stdio files (fopen, fread, fwrite, fseek, ftell, fclose), written from ISO/IEC 9899 7.21.5 and 7.21.8-7.21.9 ----
   A file opened for reading is read whole into memory at fopen; a file opened for writing collects the bytes and writes them
   at fclose (or when "a" is used, appends them). No buffering modes, no ungetc, no text-mode newline translation. */
void free(void *p);
void *memcpy(void *d, const void *s, unsigned long long n);
typedef struct FILE {
    char *data;
    long long size;
    long long cap;
    long long pos;
    int writing;
    int append;
    char path[260];
} FILE;

FILE *fopen(const char *path, const char *mode) {
    FILE *f;
    int i;
    f = (FILE *)malloc(sizeof(FILE));
    if (f == 0) return 0;
    f->data = 0;
    f->size = 0;
    f->cap = 0;
    f->pos = 0;
    f->append = 0;
    f->writing = mode[0] == 'w' || mode[0] == 'a';
    if (mode[0] == 'a') f->append = 1;
    for (i = 0; i < 259 && path[i] != 0; ++i) f->path[i] = path[i];
    f->path[i] = 0;
    if (!f->writing) {
        long long n = okay_read_file(path, &f->data);
        if (n < 0) {
            free(f);
            return 0;
        }
        f->size = n;
        f->cap = n;
    }
    return f;
}

unsigned long long fread(void *buf, unsigned long long size, unsigned long long count, FILE *f) {
    unsigned long long total = size * count;
    unsigned long long avail;
    if (f == 0 || f->writing || size == 0) return 0;
    avail = (unsigned long long)(f->size - f->pos);
    if (total > avail) total = avail - avail % size;
    memcpy(buf, f->data + f->pos, total);
    f->pos += (long long)total;
    return total / size;
}

unsigned long long fwrite(const void *buf, unsigned long long size, unsigned long long count, FILE *f) {
    unsigned long long total = size * count;
    if (f == 0 || !f->writing || size == 0) return 0;
    if ((long long)total + f->size > f->cap) {
        long long ncap = (f->cap + (long long)total) * 2 + 64;
        char *nd = (char *)malloc((unsigned long long)ncap);
        if (nd == 0) return 0;
        if (f->size > 0) memcpy(nd, f->data, (unsigned long long)f->size);
        free(f->data);
        f->data = nd;
        f->cap = ncap;
    }
    memcpy(f->data + f->size, buf, total);
    f->size += (long long)total;
    f->pos = f->size;
    return count;
}

int fseek(FILE *f, long offset, int whence) {
    long long target;
    if (f == 0) return -1;
    target = whence == 0 ? offset : whence == 1 ? f->pos + offset : f->size + offset;
    if (target < 0 || target > f->size) return -1;
    f->pos = target;
    return 0;
}

long ftell(FILE *f) {
    if (f == 0) return -1;
    return (long)f->pos;
}

int fclose(FILE *f) {
    int ok = 0;
    if (f == 0) return -1;
    if (f->writing && !okay_write_file(f->path, f->data, f->size, f->append)) ok = -1;
    if (f->data != 0) free(f->data);
    free(f);
    return ok;
}

/* ---- standard handles for <iostream> ---- */
__declspec(dllimport) void *GetStdHandle(unsigned long which);

/* Writes to standard output (which 1) or standard error (which 2). */
void okay_write_handle(int which, const char *data, long long n) {
    void *h = GetStdHandle(which == 2 ? (unsigned long)-12 : (unsigned long)-11);
    unsigned long done = 0;
    if (n > 0) WriteFile(h, data, (unsigned long)n, &done, 0);
}

/* Reads all of standard input into a new block (free it with free); returns its size. */
long long okay_read_stdin(char **data) {
    void *h = GetStdHandle((unsigned long)-10);
    char *block = (char *)malloc(65536);
    long long size = 0;
    long long cap = 65536;
    unsigned long done = 0;
    if (block == 0) return -1;
    for (;;) {
        if (size + 4096 > cap) {
            char *bigger = (char *)malloc((unsigned long long)cap * 2);
            if (bigger == 0) break;
            memcpy(bigger, block, (unsigned long long)size);
            free(block);
            block = bigger;
            cap *= 2;
        }
        done = 0;
        if (!ReadFile(h, block + size, 4096, &done, 0) || done == 0) break;
        size += (long long)done;
    }
    block[size] = 0;
    *data = block;
    return size;
}
