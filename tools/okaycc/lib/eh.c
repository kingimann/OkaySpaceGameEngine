/* Exceptions for okay-cc: a stack of records, written from ISO/IEC 14882 [except] (not the
 * Itanium C++ ABI; it cannot interoperate with libstdc++). A record is 14 longs:
 *   0 next, 1 kind (0 object, 1 array, 2 try frame, 3 handler), 2 object or exception,
 *   3 destructor (a function address) or the exception's destructor, 4 count, 5 element size,
 *   6..13 the saved registers of a try frame (see __okay_setjmp). A handler record keeps in 5 the
 *   shared block of its exception (exception_ptr, below) or 0. */
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
long __okay_exc_block; /* the shared block of the exception being thrown, or 0 */

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

/* exception_ptr ([propagation]): a shared block of 5 longs owns a thrown object once
 * current_exception() has seen it: 0 references, 1 object, 2 types, 3 destructor. Each
 * exception_ptr and each handler of that exception holds one reference; the last one destroys
 * and frees the object. */
void __okay_eh_release(long *b) {
    if (b == 0) return;
    b[0] = b[0] - 1;
    if (b[0] == 0) {
        destroy_object(b[1], b[3]);
        free((void *)b[1]);
        free((void *)b);
    }
}

void __okay_eh_retain(long *b) {
    if (b != 0) b[0] = b[0] + 1;
}

/* The end of a handler record: its exception is freed, or its block loses a reference. */
static void end_handler(long *r) {
    if (r[5] != 0) {
        __okay_eh_release((long *)r[5]);
    } else if (r[2] != 0) {
        destroy_object(r[2], r[3]);
        free((void *)r[2]);
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
            end_handler(r);
        } else {
            __okay_longjmp(&r[6], 1);
        }
    }
}

void __okay_throw(long obj, char *types, long dtor) {
    __okay_exc_block = 0;
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
    rec[5] = __okay_exc_block;
    __okay_exc_block = 0;
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
    end_handler(r);
}

void __okay_eh_resume(void) { unwind(); }

void __okay_eh_rethrow(void) {
    long *r;
    r = top_handler();
    if (r == 0) terminate_now();
    __okay_exc_obj = r[2];
    __okay_exc_dtor = r[3];
    __okay_exc_types = (char *)r[4];
    __okay_exc_block = r[5]; /* the handler's reference goes with the exception */
    r[2] = 0;
    r[5] = 0;
    unwind();
}

/* current_exception(): the block of the exception being handled (made on first use), with one
 * more reference for the caller; 0 outside a handler. */
long *__okay_eh_capture(void) {
    long *r;
    long *b;
    r = top_handler();
    if (r == 0 || r[2] == 0) return (long *)0;
    if (r[5] == 0) {
        b = (long *)malloc(5 * 8);
        b[0] = 1;
        b[1] = r[2];
        b[2] = r[4];
        b[3] = r[3];
        b[4] = 0;
        r[5] = (long)b;
    }
    b = (long *)r[5];
    b[0] = b[0] + 1;
    return b;
}

/* rethrow_exception(p): throws the object of block b again; the handler that catches it holds a
 * reference. A null b terminates (the standard leaves it undefined). */
void __okay_eh_rethrow_ptr(long *b) {
    if (b == 0) terminate_now();
    b[0] = b[0] + 1;
    __okay_exc_obj = b[1];
    __okay_exc_types = (char *)b[2];
    __okay_exc_dtor = b[3];
    __okay_exc_block = (long)b;
    unwind();
}

/* Types are compared by name, as each file may hold its own weak copy of a type_info. */
/* dynamic_cast<T *>(p) and dynamic_cast<void *>(p) with multiple inheritance ([expr.dynamic.cast]
 * p7-p8; Itanium C++ ABI 2.5.2: the word before a vtable part's type_info word is its
 * offset-to-top). The whole object starts at p plus that offset; its type_info (words 3 on: how
 * many bases after the primary one, then each one's type_info and offset, src/mangle.cpp) leads to
 * a T subobject and its offset. Returns its address, the whole object's for a null target, or 0. */
static long __okay_base_at(long *ti, long *target, long at) {
    long r;
    long i;
    if (!ti) return -1;
    if (ti == target || strcmp((char *)ti[1], (char *)target[1]) == 0) return at;
    r = __okay_base_at((long *)ti[2], target, at);
    if (r >= 0) return r;
    for (i = 0; i < ti[3]; i = i + 1) {
        r = __okay_base_at((long *)ti[4 + 2 * i], target, at + ti[5 + 2 * i]);
        if (r >= 0) return r;
    }
    return -1;
}
char *__okay_dyncast_ptr(char *p, long *target) {
    long *vptr;
    char *whole;
    long off;
    if (!p) return 0;
    vptr = *(long **)p;
    whole = p + vptr[-2];
    if (!target) return whole;
    off = __okay_base_at((long *)vptr[-1], target, 0);
    if (off < 0) return 0;
    return whole + off;
}
/* dynamic_cast<T &>(x): a failed cast throws std::bad_cast ([expr.dynamic.cast] p9); here it ends
 * the program as an uncaught one does (no std::bad_cast object is made yet). */
char *__okay_dyncast_ref(char *p, long *target) {
    char *r = __okay_dyncast_ptr(p, target);
    if (!r) {
        write(2, "terminate called after throwing an instance of 'std::bad_cast'\n", 63);
        write(2, "  what():  std::bad_cast\n", 25);
        syscall(62, syscall(39, 0, 0, 0), 6, 0);
        syscall(60, 134, 0, 0);
    }
    return r;
}
