/* dynamic_cast support for okay-ms, written from ISO/IEC 14882 [expr.dynamic.cast]. The compiler makes one type record per type
 * per file: { name, base } where base is the record of the single base class (or 0). The record of an object's real type is
 * stored just before its virtual table (vtable[-1]). A cast succeeds when the target's name is found on the chain from the
 * object's real type upwards (names are compared as text because every file has its own record). Single inheritance only, and
 * the base is assumed to start at the start of the object, so the pointer value does not change. */
struct okay_ti {
    char *name;
    struct okay_ti *base;
};

static int same_name(char *a, char *b) {
    while (*a != 0 && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

void *__okay_dyncast(void *p, void *target) {
    struct okay_ti *ti;
    struct okay_ti *want;
    void **vtable;
    if (p == (void *)0) return (void *)0;
    want = (struct okay_ti *)target;
    vtable = *(void ***)p;
    ti = (struct okay_ti *)vtable[-1];
    while (ti != (struct okay_ti *)0) {
        if (same_name(ti->name, want->name)) return p;
        ti = ti->base;
    }
    return (void *)0;
}
