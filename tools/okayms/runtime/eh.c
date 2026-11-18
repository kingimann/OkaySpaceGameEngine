/* Exceptions for okay-ms: a stack of records, written from ISO/IEC 14882 [except]. This is not
 * the MSVC C++ exception model (no .pdata handler, no ThrowInfo, no __CxxFrameHandler), so it
 * cannot interoperate with code that throws that way. A record is 16 pointers:
 *   0 next, 1 kind (0 object, 1 array, 2 try frame, 3 handler), 2 object or exception,
 *   3 destructor or the exception's destructor, 4 count or the type names, 5 element size,
 *   6..15 the registers of a try frame (see __okay_setjmp in compile.cpp). */
__declspec(dllimport) void ExitProcess(unsigned int code);
__declspec(dllimport) void *GetStdHandle(unsigned int which);
__declspec(dllimport) int WriteFile(void *file, char *data, unsigned int n, unsigned int *written, void *overlapped);
void *malloc(unsigned int n);
void free(void *p);

int __okay_setjmp(void *buf);
void __okay_longjmp(void *buf);

void *__okay_eh_top;
void *__okay_exc_obj;
char *__okay_exc_types;
void *__okay_exc_dtor;

void __okay_eh_push(void *rec) {
    void **r;
    r = (void **)rec;
    r[0] = __okay_eh_top;
    __okay_eh_top = rec;
}

void __okay_eh_pop(void) {
    void **r;
    r = (void **)__okay_eh_top;
    __okay_eh_top = r[0];
}

static void terminate_now(void) {
    char *msg;
    unsigned int written;
    msg = "terminate called after throwing an exception\r\n";
    WriteFile(GetStdHandle(4294967284), msg, 46, &written, (void *)0);
    ExitProcess(3);
}

typedef void (*dtor_fn)(void *);

static void destroy_object(void *obj, void *dtor) {
    dtor_fn d;
    if (dtor != (void *)0) {
        d = (dtor_fn)dtor;
        d(obj);
    }
}

/* Walks the records from the top: objects are destroyed, a handler record frees its exception, and
 * the first try frame is entered (its handlers then test the type). Nothing left: terminate. */
static void unwind(void) {
    void **r;
    int i;
    int kind;
    for (;;) {
        r = (void **)__okay_eh_top;
        if (r == (void **)0) terminate_now();
        __okay_eh_top = r[0];
        kind = (int)r[1];
        if (kind == 0) {
            destroy_object(r[2], r[3]);
        } else if (kind == 1) {
            i = (int)r[4];
            while (i > 0) {
                i = i - 1;
                destroy_object((void *)((char *)r[2] + i * (int)r[5]), r[3]);
            }
        } else if (kind == 3) {
            if (r[2] != (void *)0) {
                destroy_object(r[2], r[3]);
                free(r[2]);
            }
        } else {
            __okay_longjmp((void *)&r[6]);
        }
    }
}

void __okay_throw(void *obj, int size, char *types, void *dtor) {
    char *copy;
    char *from;
    int i;
    copy = (char *)malloc(size);
    from = (char *)obj;
    for (i = 0; i < size; i = i + 1) copy[i] = from[i];
    __okay_exc_obj = (void *)copy;
    __okay_exc_types = types;
    __okay_exc_dtor = dtor;
    unwind();
}

/* The names in 'types' are separated by newlines: the thrown class first, then its bases. */
int __okay_eh_match(char *name) {
    char *t;
    char *e;
    char *n;
    int k;
    t = __okay_exc_types;
    while (*t != 0) {
        e = t;
        n = name;
        while (*e != 0 && *e != 10 && *n != 0 && *e == *n) {
            e = e + 1;
            n = n + 1;
        }
        if (*n == 0 && (*e == 0 || *e == 10)) return 1;
        while (*e != 0 && *e != 10) e = e + 1;
        if (*e == 0) return 0;
        t = e + 1;
    }
    return 0;
}

void __okay_eh_begin(void *rec) {
    void **r;
    r = (void **)rec;
    r[1] = (void *)3;
    r[2] = __okay_exc_obj;
    r[3] = __okay_exc_dtor;
    r[4] = (void *)__okay_exc_types;
    __okay_eh_push(rec);
}

static void **top_handler(void) {
    void **r;
    r = (void **)__okay_eh_top;
    while (r != (void **)0 && (int)r[1] != 3) r = (void **)r[0];
    return r;
}

void *__okay_eh_obj(void) {
    void **r;
    r = top_handler();
    if (r == (void **)0) return (void *)0;
    return r[2];
}

void __okay_eh_end(void) {
    void **r;
    r = (void **)__okay_eh_top;
    __okay_eh_top = r[0];
    if (r[2] != (void *)0) {
        destroy_object(r[2], r[3]);
        free(r[2]);
    }
}

void __okay_eh_resume(void) { unwind(); }

void __okay_eh_rethrow(void) {
    void **r;
    r = top_handler();
    if (r == (void **)0) terminate_now();
    __okay_exc_obj = r[2];
    __okay_exc_dtor = r[3];
    __okay_exc_types = (char *)r[4];
    r[2] = (void *)0;
    unwind();
}
