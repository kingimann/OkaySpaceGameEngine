__declspec(dllimport) void *GetProcessHeap(void);
__declspec(dllimport) void *HeapAlloc(void *heap, unsigned int flags, unsigned int bytes);
__declspec(dllimport) int HeapFree(void *heap, unsigned int flags, void *mem);

void *malloc(unsigned int n) { return HeapAlloc(GetProcessHeap(), 0, n); }

void free(void *p) {
    if (p != 0) HeapFree(GetProcessHeap(), 0, p);
}

void *calloc(unsigned int count, unsigned int size) {
    /* HEAP_ZERO_MEMORY is 8 */
    return HeapAlloc(GetProcessHeap(), 8, count * size);
}
