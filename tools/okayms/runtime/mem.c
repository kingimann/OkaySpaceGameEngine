__declspec(dllimport) void *GetProcessHeap(void);
/* HeapAlloc's size is a SIZE_T: 64 bits on x64 (Round 33: it was declared 32 bits, so a size of 4 GiB or more was cut). */
__declspec(dllimport) void *HeapAlloc(void *heap, unsigned int flags, unsigned long long bytes);
__declspec(dllimport) int HeapFree(void *heap, unsigned int flags, void *mem);

void *malloc(unsigned long long n) { return HeapAlloc(GetProcessHeap(), 0, n); }

void free(void *p) {
    if (p != 0) HeapFree(GetProcessHeap(), 0, p);
}

void *calloc(unsigned long long count, unsigned long long size) {
    /* ISO/IEC 9899 7.22.3.2: null when count * size does not fit (Round 33: it wrapped). HEAP_ZERO_MEMORY is 8. */
    if (size != 0 && count > 0xFFFFFFFFFFFFFFFFull / size) return 0;
    return HeapAlloc(GetProcessHeap(), 8, count * size);
}
