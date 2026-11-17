typedef char *va_list;

__declspec(dllimport) void *GetStdHandle(unsigned int n);
__declspec(dllimport) int WriteFile(void *h, void *buf, unsigned int n, unsigned int *wrote, void *ov);

void put_u(char *out, unsigned int *n, unsigned int v, unsigned int base) {
    char tmp[16];
    int i;
    unsigned int digit;
    if (v == 0) {
        out[*n] = '0';
        *n = *n + 1;
        return;
    }
    i = 0;
    while (v != 0 && i < 16) {
        digit = v % base;
        if (digit < 10) tmp[i] = '0' + digit;
        else tmp[i] = 'a' + digit - 10;
        i = i + 1;
        v = v / base;
    }
    while (i > 0) {
        i = i - 1;
        out[*n] = tmp[i];
        *n = *n + 1;
    }
}

int printf(char *fmt, ...) {
    char out[128];
    unsigned int n;
    unsigned int wrote;
    va_list ap;
    n = 0;
    va_start(ap, fmt);
    while (fmt[0] != 0 && n < 120) {
        if (fmt[0] == '%' && fmt[1] == 'd') {
            int v;
            unsigned int u;
            v = va_arg(ap, int);
            if (v < 0) {
                out[n] = '-';
                n = n + 1;
                v = -v;
            }
            u = v;
            put_u(out, &n, u, 10);
            fmt = fmt + 2;
        } else if (fmt[0] == '%' && fmt[1] == 'x') {
            unsigned int v;
            v = va_arg(ap, unsigned int);
            put_u(out, &n, v, 16);
            fmt = fmt + 2;
        } else if (fmt[0] == '%' && fmt[1] == 'f') {
            double fval;
            int fwhole;
            int fi;
            double ffrac;
            fval = va_arg(ap, double);
            if (fval < 0) {
                out[n] = '-';
                n = n + 1;
                fval = -fval;
            }
            fwhole = (int)fval;
            put_u(out, &n, fwhole, 10);
            out[n] = '.';
            n = n + 1;
            ffrac = fval - (double)fwhole;
            for (fi = 0; fi < 6; fi = fi + 1) {
                int fdigit;
                ffrac = ffrac * 10.0;
                fdigit = (int)ffrac;
                out[n] = '0' + fdigit;
                n = n + 1;
                ffrac = ffrac - (double)fdigit;
            }
            fmt = fmt + 2;
        } else if (fmt[0] == '%' && fmt[1] == 'c') {
            int v;
            v = va_arg(ap, int);
            out[n] = v;
            n = n + 1;
            fmt = fmt + 2;
        } else if (fmt[0] == '%' && fmt[1] == 's') {
            char *s;
            unsigned int i;
            s = va_arg(ap, char *);
            i = 0;
            while (s[i] != 0 && n < 120) {
                out[n] = s[i];
                n = n + 1;
                i = i + 1;
            }
            fmt = fmt + 2;
        } else {
            out[n] = fmt[0];
            n = n + 1;
            fmt = fmt + 1;
        }
    }
    va_end(ap);
    WriteFile(GetStdHandle(0xFFFFFFF5), out, n, &wrote, 0);
    return n;
}

int snprintf(char *buf, unsigned int cap, char *fmt, ...) {
    unsigned int n;
    va_list ap;
    if (cap == 0) return 0;
    n = 0;
    va_start(ap, fmt);
    while (fmt[0] != 0 && n + 1 < cap) {
        if (fmt[0] == '%' && fmt[1] == 'd') {
            int v;
            char tmp[16];
            int i;
            unsigned int u;
            v = va_arg(ap, int);
            if (v < 0 && n + 1 < cap) {
                buf[n] = '-';
                n = n + 1;
                v = -v;
            }
            u = v;
            i = 0;
            if (u == 0) tmp[i++] = '0';
            while (u != 0 && i < 16) {
                tmp[i] = '0' + (u % 10);
                i = i + 1;
                u = u / 10;
            }
            while (i > 0 && n + 1 < cap) {
                i = i - 1;
                buf[n] = tmp[i];
                n = n + 1;
            }
            fmt = fmt + 2;
        } else if (fmt[0] == '%' && fmt[1] == 's') {
            char *s;
            unsigned int i;
            s = va_arg(ap, char *);
            i = 0;
            while (s[i] != 0 && n + 1 < cap) {
                buf[n] = s[i];
                n = n + 1;
                i = i + 1;
            }
            fmt = fmt + 2;
        } else {
            buf[n] = fmt[0];
            n = n + 1;
            fmt = fmt + 1;
        }
    }
    buf[n] = 0;
    va_end(ap);
    return n;
}

int puts(char *s) { return printf("%s\n", s); }
