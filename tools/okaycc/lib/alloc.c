long syscall(long n, long a, long b, long c);
void *memset(void *dst, int c, int n);
void *memcpy(void *dst, void *src, int n);

struct Blk {
    long size;
    long used;
    struct Blk *prev;
    struct Blk *next;
};

struct Blk *freep;
struct Blk *heap_lo;
char *heap_hi;
int heap_on;

char *blk_mem(struct Blk *b) { return (char *)b + 32; }
struct Blk **blk_flink(struct Blk *b) { return (struct Blk **)blk_mem(b); }
struct Blk **blk_blink(struct Blk *b) { return (struct Blk **)(blk_mem(b) + 8); }

void blk_unlink(struct Blk *b) {
    struct Blk *n;
    struct Blk *p;
    n = *blk_flink(b);
    p = *blk_blink(b);
    if (p) *blk_flink(p) = n;
    else freep = n;
    if (n) *blk_blink(n) = p;
}

void blk_insert(struct Blk *b) {
    *blk_flink(b) = freep;
    *blk_blink(b) = 0;
    if (freep) *blk_blink(freep) = b;
    freep = b;
}

int blk_round(int n) {
    if (n < 16) n = 16;
    if (n % 16) n = n + (16 - (n % 16));
    return n;
}

int blk_grow(int n) {
    long cur;
    long base;
    long need;
    long got;
    struct Blk *b;
    int span;
    if (!heap_on) {
        cur = syscall(12, 0, 0, 0);
        base = (cur + 15) & ~15L;
        need = base + 8192;
        if (need < base + 32 + n) need = base + 32 + n + 256;
        got = syscall(12, need, 0, 0);
        if (got < base + 32 + n) return 0;
        b = (struct Blk *)base;
        span = (int)(got - base - 32);
        span = span - (span % 16);
        b->size = span;
        b->used = 0;
        b->prev = 0;
        b->next = 0;
        heap_lo = b;
        heap_hi = (char *)got;
        heap_on = 1;
        blk_insert(b);
        return 1;
    }
    cur = (long)heap_hi;
    need = cur + 32 + n;
    got = syscall(12, need, 0, 0);
    if (got < need) return 0;
    b = (struct Blk *)cur;
    span = (int)(got - cur - 32);
    span = span - (span % 16);
    b->size = span;
    b->used = 0;
    b->next = 0;
    b->prev = 0;
    if (heap_lo) {
        struct Blk *tail;
        tail = heap_lo;
        while (tail->next) tail = tail->next;
        tail->next = b;
        b->prev = tail;
    } else {
        heap_lo = b;
    }
    heap_hi = (char *)got;
    blk_insert(b);
    return 1;
}

struct Blk *blk_take(int n) {
    struct Blk *b;
    struct Blk *rest;
    b = freep;
    while (b) {
        if (b->size >= n) break;
        b = *blk_flink(b);
    }
    if (!b) return 0;
    blk_unlink(b);
    if (b->size - n >= 48) {
        rest = (struct Blk *)(blk_mem(b) + n);
        rest->size = b->size - n - 32;
        rest->used = 0;
        rest->prev = b;
        rest->next = b->next;
        if (rest->next) rest->next->prev = rest;
        b->next = rest;
        b->size = n;
        blk_insert(rest);
    }
    b->used = 1;
    return b;
}

char *malloc(int n) {
    struct Blk *b;
    n = blk_round(n);
    b = blk_take(n);
    if (!b) {
        if (!blk_grow(n)) return 0;
        b = blk_take(n);
        if (!b) return 0;
    }
    return blk_mem(b);
}

void free(char *p) {
    struct Blk *b;
    struct Blk *n;
    struct Blk *prev;
    if (p == 0) return;
    b = (struct Blk *)(p - 32);
    if (!b->used) return;
    b->used = 0;
    n = b->next;
    if (n && !n->used) {
        blk_unlink(n);
        b->size = b->size + 32 + n->size;
        b->next = n->next;
        if (b->next) b->next->prev = b;
    }
    prev = b->prev;
    if (prev && !prev->used) {
        blk_unlink(prev);
        prev->size = prev->size + 32 + b->size;
        prev->next = b->next;
        if (prev->next) prev->next->prev = prev;
        b = prev;
    }
    blk_insert(b);
}

char *realloc(char *p, int n) {
    struct Blk *b;
    char *np;
    int old;
    int need;
    if (p == 0) return malloc(n);
    if (n <= 0) {
        free(p);
        return 0;
    }
    b = (struct Blk *)(p - 32);
    need = blk_round(n);
    old = (int)b->size;
    if (need <= old) {
        if (old - need >= 48) {
            struct Blk *rest;
            rest = (struct Blk *)(blk_mem(b) + need);
            rest->size = old - need - 32;
            rest->used = 1;
            rest->prev = b;
            rest->next = b->next;
            if (rest->next) rest->next->prev = rest;
            b->next = rest;
            b->size = need;
            free(blk_mem(rest));
        }
        return p;
    }
    np = malloc(n);
    if (np == 0) return 0;
    memcpy(np, p, old);
    free(p);
    return np;
}

char *calloc(int count, int size) {
    int n;
    char *p;
    if (count < 0 || size < 0) return 0;
    if (size != 0 && count > 2147483647 / size) return 0;
    n = count * size;
    p = malloc(n);
    if (p == 0) return 0;
    memset(p, 0, n);
    return p;
}
