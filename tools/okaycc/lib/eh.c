/* Exceptions for okay-cc: a stack of records, written from ISO/IEC 14882 [except] (not the
 * Itanium C++ ABI; it cannot interoperate with libstdc++). A record is 14 longs:
 *   0 next, 1 kind (0 object, 1 array, 2 try frame, 3 handler), 2 object or exception,
 *   3 destructor (a function address) or the exception's destructor, 4 count, 5 element size,
 *   6..13 the saved registers of a try frame (see __okay_setjmp). */
long syscall(long n, long a, long b, long c);
void *malloc(long n);
void free(void *p);
int write(int fd, char *buf, int n);
long strlen(char *s);
int strcmp(char *a, char *b);

long __okay_setjmp(long *buf);
void __okay_longjmp(long *buf, long val);

long __okay_eh_top;
long __okay_exc_obj;
char *__okay_exc_types;
long __okay_exc_dtor;

void __okay_eh_push(long *rec) {
    rec[0] = __okay_eh_top;
    __okay_eh_top = (long)rec;
}

void __okay_eh_pop(void) {
    long *r;
    r = (long *)__okay_eh_top;
    __okay_eh_top = r[0];
}

void *__okay_alloc(long n) { return malloc(n); }

static void terminate_now(void) {
    char *msg;
    long pid;
    msg = "terminate called after throwing an exception\n";
    write(2, msg, (int)strlen(msg));
    pid = syscall(39, 0, 0, 0);
    syscall(62, pid, 6, 0);
    syscall(60, 134, 0, 0);
}

static void destroy_object(long obj, long dtor) {
    void (*d)(void *);
    if (dtor != 0) {
        d = (void (*)(void *))dtor;
        d((void *)obj);
    }
}

/* Walks the records from the top: objects are destroyed, a handler record frees its exception, and
 * the first try frame is entered (its handlers then test the type). Nothing left: terminate. */
static void unwind(void) {
    long *r;
    long i;
    long *p;
    for (;;) {
        r = (long *)__okay_eh_top;
        if (r == 0) terminate_now();
        __okay_eh_top = r[0];
        if (r[1] == 0) {
            destroy_object(r[2], r[3]);
        } else if (r[1] == 1) {
            i = r[4];
            while (i > 0) {
                i = i - 1;
                destroy_object(r[2] + i * r[5], r[3]);
            }
        } else if (r[1] == 3) {
            if (r[2] != 0) {
                destroy_object(r[2], r[3]);
                free((void *)r[2]);
            }
        } else {
            __okay_longjmp(&r[6], 1);
        }
    }
}

void __okay_throw(long obj, char *types, long dtor) {
    __okay_exc_obj = obj;
    __okay_exc_types = types;
    __okay_exc_dtor = dtor;
    unwind();
}

/* The names in 'types' are separated by newlines: the thrown class first, then its bases. */
int __okay_eh_match(char *name) {
    char *t;
    char *e;
    long n;
    n = strlen(name);
    t = __okay_exc_types;
    while (*t != 0) {
        e = t;
        while (*e != 0 && *e != 10) e = e + 1;
        if (e - t == n) {
            long k;
            k = 0;
            while (k < n && t[k] == name[k]) k = k + 1;
            if (k == n) return 1;
        }
        t = *e == 0 ? e : e + 1;
    }
    return 0;
}

void __okay_eh_begin(long *rec) {
    rec[1] = 3;
    rec[2] = __okay_exc_obj;
    rec[3] = __okay_exc_dtor;
    rec[4] = (long)__okay_exc_types;
    __okay_eh_push(rec);
}

static long *top_handler(void) {
    long *r;
    r = (long *)__okay_eh_top;
    while (r != 0 && r[1] != 3) r = (long *)r[0];
    return r;
}

void *__okay_eh_obj(void) {
    long *r;
    r = top_handler();
    return r == 0 ? (void *)0 : (void *)r[2];
}

void __okay_eh_end(void) {
    long *r;
    r = (long *)__okay_eh_top;
    __okay_eh_top = r[0];
    if (r[2] != 0) {
        destroy_object(r[2], r[3]);
        free((void *)r[2]);
    }
}

void __okay_eh_resume(void) { unwind(); }

void __okay_eh_rethrow(void) {
    long *r;
    r = top_handler();
    if (r == 0) terminate_now();
    __okay_exc_obj = r[2];
    __okay_exc_dtor = r[3];
    __okay_exc_types = (char *)r[4];
    r[2] = 0;
    unwind();
}

/* dynamic_cast<T *>(p) for single inheritance ([expr.dynamic.cast] p5-p8): the object's type_info
 * (from its vtable) is T's, or one of its base links leads to T's. A type_info object is three
 * words (src/mangle.cpp makeTypeInfo): 0 unused, 1 the name, 2 the base class's type_info or 0.
 * Types are compared by name, as each file may hold its own weak copy. Returns 1 or 0. */
int __okay_dyncast(long *ti, long *target) {
    while (ti) {
        if (ti == target || strcmp((char *)ti[1], (char *)target[1]) == 0) return 1;
        ti = (long *)ti[2];
    }
    return 0;
}
