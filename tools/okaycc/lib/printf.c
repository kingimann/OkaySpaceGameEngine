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
int p_fixed(double v) {
    int n;
    int ip;
    int frac;
    int place;
    int dig;
    n = 0;
    if (v < 0) {
        n = n + p_char('-');
        v = 0.0 - v;
    }
    ip = (int)v;
    frac = (int)((v - (double)ip) * 1000000.0 + 0.5);
    if (frac >= 1000000) {
        frac = frac - 1000000;
        ip = ip + 1;
    }
    if (frac < 0) frac = 0;
    n = n + p_dec(ip);
    n = n + p_char('.');
    place = 100000;
    while (place > 0) {
        dig = frac / place;
        n = n + p_char(48 + dig);
        frac = frac - dig * place;
        place = place / 10;
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
            {
                int left;
                int zero;
                int width;
                int islong;
                left = 0;
                zero = 0;
                width = 0;
                islong = 0;
                if (fmt[i] == '-') {
                    left = 1;
                    i = i + 1;
                }
                if (fmt[i] == '0') {
                    zero = 1;
                    i = i + 1;
                }
                while (fmt[i] >= '0' && fmt[i] <= '9') {
                    width = width * 10 + (fmt[i] - '0');
                    i = i + 1;
                }
                if (fmt[i] == 'l') {
                    islong = 1;
                    i = i + 1;
                }
                if (fmt[i] == 'd' || fmt[i] == 'u' || fmt[i] == 'x') {
                    char buf[32];
                    int len;
                    int k;
                    unsigned u;
                    int neg;
                    int base;
                    char *digits;
                    neg = 0;
                    base = 10;
                    digits = "0123456789abcdef";
                    if (fmt[i] == 'x') base = 16;
                    if (fmt[i] == 'd') {
                        long v;
                        if (islong) v = va_arg(ap, long);
                        else v = va_arg(ap, int);
                        if (v < 0) {
                            neg = 1;
                            u = (unsigned)0 - (unsigned)v;
                        } else {
                            u = (unsigned)v;
                        }
                    } else if (islong) {
                        u = (unsigned)va_arg(ap, long);
                    } else {
                        u = va_arg(ap, unsigned);
                    }
                    len = 0;
                    if (u == 0) {
                        buf[0] = '0';
                        len = 1;
                    }
                    while (u != 0) {
                        buf[len] = digits[u % (unsigned)base];
                        len = len + 1;
                        u = u / (unsigned)base;
                    }
                    if (neg) {
                        n = n + p_char('-');
                    }
                    k = width - len;
                    if (neg) k = k - 1;
                    if (k < 0) k = 0;
                    if (!left) {
                        while (k > 0) {
                            n = n + p_char(zero ? '0' : ' ');
                            k = k - 1;
                        }
                    }
                    while (len > 0) {
                        len = len - 1;
                        n = n + p_char(buf[len]);
                    }
                    if (left) {
                        while (k > 0) {
                            n = n + p_char(' ');
                            k = k - 1;
                        }
                    }
                    i = i + 1;
                } else if (fmt[i] == 's') {
                    char *str;
                    int len;
                    int k;
                    str = va_arg(ap, char *);
                    if (str == 0) str = "(null)";
                    len = 0;
                    while (str[len] != 0) len = len + 1;
                    k = width - len;
                    if (k < 0) k = 0;
                    if (!left) {
                        while (k > 0) {
                            n = n + p_char(' ');
                            k = k - 1;
                        }
                    }
                    n = n + p_str(str);
                    if (left) {
                        while (k > 0) {
                            n = n + p_char(' ');
                            k = k - 1;
                        }
                    }
                    i = i + 1;
                } else if (fmt[i] == 'f') {
                    n = n + p_fixed(va_arg(ap, double));
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
    }
    va_end(ap);
    return n;
}
