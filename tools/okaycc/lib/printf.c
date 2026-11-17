#if defined(__okaycc__)
#include "stdarg.h"
#else
#include <stdarg.h>
#endif
int write(int fd, char *buf, int n);
int p_char(int c) {
    char b;
    b = (char)c;
    if (write(1, &b, 1) != 1) return 0;
    return 1;
}
int p_str(char *s) {
    int n;
    int i;
    n = 0;
    i = 0;
    if (s == 0) s = "(null)";
    while (s[i] != 0) {
        n = n + p_char(s[i]);
        i = i + 1;
    }
    return n;
}
int p_dec(int v) {
    char buf[16];
    unsigned u;
    int i;
    int n;
    n = 0;
    if (v < 0) {
        n = n + p_char('-');
        u = (unsigned)0 - (unsigned)v;
    } else {
        u = (unsigned)v;
    }
    i = 0;
    if (u == 0) {
        buf[0] = '0';
        i = 1;
    }
    while (u != 0) {
        buf[i] = (char)(48 + (int)(u % 10));
        i = i + 1;
        u = u / 10;
    }
    while (i > 0) {
        i = i - 1;
        n = n + p_char(buf[i]);
    }
    return n;
}
int p_hex(unsigned v) {
    char buf[16];
    int i;
    int n;
    char *digits;
    digits = "0123456789abcdef";
    i = 0;
    n = 0;
    if (v == 0) {
        buf[0] = '0';
        i = 1;
    }
    while (v != 0) {
        buf[i] = digits[v % 16];
        i = i + 1;
        v = v / 16;
    }
    while (i > 0) {
        i = i - 1;
        n = n + p_char(buf[i]);
    }
    return n;
}
int printf(char *fmt, ...) {
    va_list ap;
    int i;
    int n;
    i = 0;
    n = 0;
    va_start(ap, fmt);
    while (fmt[i] != 0) {
        if (fmt[i] != '%') {
            n = n + p_char(fmt[i]);
            i = i + 1;
        } else {
            i = i + 1;
            if (fmt[i] == 'd') {
                n = n + p_dec(va_arg(ap, int));
                i = i + 1;
            } else if (fmt[i] == 's') {
                n = n + p_str(va_arg(ap, char *));
                i = i + 1;
            } else if (fmt[i] == 'x') {
                n = n + p_hex(va_arg(ap, unsigned));
                i = i + 1;
            } else if (fmt[i] == 'c') {
                n = n + p_char(va_arg(ap, int));
                i = i + 1;
            } else if (fmt[i] == '%') {
                n = n + p_char('%');
                i = i + 1;
            } else if (fmt[i] == 0) {
                n = n + p_char('%');
            } else {
                n = n + p_char('%');
                n = n + p_char(fmt[i]);
                i = i + 1;
            }
        }
    }
    va_end(ap);
    return n;
}
