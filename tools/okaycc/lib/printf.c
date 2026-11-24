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
/* Exact decimal digits of a double (ISO C 7.21.6.1 %f and %e, rounded as IEEE 754 round to
 * nearest, ties to even: the result glibc gives). A finite double is M * 2^E with M < 2^53, so its
 * decimal expansion is finite: the integer part is M << E written out by dividing by 10, the
 * fraction (when E < 0) is a fixed-point number of -E bits multiplied by 10 one digit at a time.
 * The value is 0.d1 d2 d3 ... times 10^dp. */
static char gDig[1200];
static int gLen;
static int gDp;
static void expandDouble(double v) {
    unsigned long bits = *(unsigned long *)&v;
    unsigned long m = bits & 4503599627370495ul;
    int e = (int)((bits >> 52) & 2047);
    unsigned int w[40];
    char ib[400];
    int ni;
    int k;
    gLen = 0;
    gDp = 0;
    if (e == 0) e = 1; /* subnormal */
    else m = m | 4503599627370496ul;
    e = e - 1075;
    if (m == 0) return;
    for (k = 0; k < 40; k = k + 1) w[k] = 0;
    ni = 0;
    if (e >= 0) {
        /* the integer M << e, then its digits from the right */
        int sh = e;
        int word = sh / 32;
        int bit = sh % 32;
        unsigned long lo = m << bit;
        unsigned long hi = bit == 0 ? 0 : (m >> (64 - bit));
        int top = 39;
        w[word] = (unsigned int)(lo & 4294967295ul);
        if (word + 1 < 40) w[word + 1] = (unsigned int)(lo >> 32);
        if (word + 2 < 40) w[word + 2] = (unsigned int)(hi & 4294967295ul);
        for (;;) {
            unsigned long rem = 0;
            int any = 0;
            for (k = top; k >= 0; k = k - 1) {
                unsigned long cur = (rem << 32) | (unsigned long)w[k];
                w[k] = (unsigned int)(cur / 10ul);
                rem = cur % 10ul;
                if (w[k] != 0) any = 1;
            }
            ib[ni] = (char)('0' + (int)rem);
            ni = ni + 1;
            if (!any) break;
        }
        for (k = 0; k < ni; k = k + 1) gDig[k] = ib[ni - 1 - k];
        gLen = ni;
        gDp = ni;
        return;
    }
    {
        /* integer part (fits in 64 bits since -e >= 1) and the fraction bits */
        int fb = -e;
        unsigned long ip = fb >= 64 ? 0 : (m >> fb);
        int nw = (fb + 31) / 32 + 1;
        int lead = 1;
        if (ip != 0) {
            unsigned long t = ip;
            while (t != 0) {
                ib[ni] = (char)('0' + (int)(t % 10ul));
                ni = ni + 1;
                t = t / 10ul;
            }
            for (k = 0; k < ni; k = k + 1) gDig[k] = ib[ni - 1 - k];
            gLen = ni;
            gDp = ni;
            lead = 0;
        }
        /* fraction = m mod 2^fb, as fb bits in w[] */
        if (fb < 64) m = m & ((1ul << fb) - 1ul);
        {
            int word = 0;
            while (m != 0) {
                w[word] = (unsigned int)(m & 4294967295ul);
                m = m >> 32;
                word = word + 1;
            }
        }
        for (;;) {
            unsigned long carry = 0;
            int any = 0;
            int d;
            for (k = 0; k < nw; k = k + 1) {
                unsigned long cur = (unsigned long)w[k] * 10ul + carry;
                w[k] = (unsigned int)(cur & 4294967295ul);
                carry = cur >> 32;
            }
            /* the digit is the part at and above bit fb */
            {
                int word = fb / 32;
                int bit = fb % 32;
                unsigned long above = (unsigned long)w[word] >> bit;
                for (k = word + 1; k < nw; k = k + 1) above = above + ((unsigned long)w[k] << (32 - bit + 32 * (k - word - 1)));
                d = (int)above;
                w[word] = bit == 0 ? 0 : (w[word] & (unsigned int)((1ul << bit) - 1ul));
                for (k = word + 1; k < nw; k = k + 1) w[k] = 0;
            }
            for (k = 0; k < nw; k = k + 1)
                if (w[k] != 0) any = 1;
            if (lead && d == 0) {
                gDp = gDp - 1; /* a leading zero after the point */
            } else {
                lead = 0;
                gDig[gLen] = (char)('0' + d);
                gLen = gLen + 1;
            }
            if (!any || gLen >= 1190) break;
        }
    }
}
/* Keep n digits of gDig, rounding half to even on everything after them. Returns 1 when the
 * rounding carried into a new leading digit (gDp moved). */
static int roundDigits(int n) {
    int up;
    int k;
    if (n < 0) {
        gLen = 0;
        return 0;
    }
    if (n >= gLen) {
        while (gLen < n) {
            gDig[gLen] = '0';
            gLen = gLen + 1;
        }
        return 0;
    }
    up = 0;
    if (gDig[n] > '5') up = 1;
    else if (gDig[n] == '5') {
        int rest = 0;
        for (k = n + 1; k < gLen; k = k + 1)
            if (gDig[k] != '0') rest = 1;
        if (rest) up = 1;
        else up = n > 0 ? ((gDig[n - 1] - '0') & 1) : 0; /* a tie: to even (0 counts as even) */
    }
    gLen = n;
    if (!up) return 0;
    k = n - 1;
    while (k >= 0 && gDig[k] == '9') {
        gDig[k] = '0';
        k = k - 1;
    }
    if (k >= 0) {
        gDig[k] = (char)(gDig[k] + 1);
        return 0;
    }
    /* all nines: 1 followed by zeros */
    for (k = gLen; k > 0; k = k - 1) gDig[k] = gDig[k - 1];
    gDig[0] = '1';
    gLen = gLen + 1;
    gDp = gDp + 1;
    return 1;
}
/* %f or %e of v with prec digits, into out (no padding). Returns the length. %g is %e when the
 * exponent X of v rounded to P significant digits is below -4 or at least P, else %f with P-1-X
 * digits; then trailing zeros and a bare point go (C 7.21.6.1, the g conversion). */
static int fmtDouble(double v, int conv, int prec, int plus, char *out);
static int fmtG(double v, int conv, int prec, int plus, char *out) {
    int p = prec == 0 ? 1 : prec;
    int x = 0;
    int n;
    int k;
    int dot;
    int ee;
    unsigned long bits = *(unsigned long *)&v;
    if (((bits >> 52) & 2047) == 2047) return fmtDouble(v, conv == 'G' ? 'E' : 'e', 0, plus, out);
    expandDouble(v);
    if (gLen > 0) {
        roundDigits(p);
        x = gDp - 1;
    }
    if (x < -4 || x >= p) n = fmtDouble(v, conv == 'G' ? 'E' : 'e', p - 1, plus, out);
    else n = fmtDouble(v, 'f', p - 1 - x, plus, out);
    dot = -1;
    ee = n;
    for (k = 0; k < n; k = k + 1) {
        if (out[k] == '.') dot = k;
        if (out[k] == 'e' || out[k] == 'E') ee = k;
    }
    if (dot < 0) return n;
    k = ee;
    while (k > dot + 1 && out[k - 1] == '0') k = k - 1;
    if (k == dot + 1) k = dot;
    {
        int j;
        int m = k;
        for (j = ee; j < n; j = j + 1) out[m++] = out[j];
        return m;
    }
}
static int fmtDouble(double v, int conv, int prec, int plus, char *out) {
    int n = 0;
    int k;
    unsigned long bits = *(unsigned long *)&v;
    int neg = (bits >> 63) != 0;
    int e = (int)((bits >> 52) & 2047);
    if (conv == 'g' || conv == 'G') return fmtG(v, conv, prec, plus, out);
    if (neg) out[n++] = '-';
    else if (plus) out[n++] = '+';
    if (e == 2047) {
        char *t = (bits & 4503599627370495ul) ? "nan" : "inf";
        if (conv == 'E' || conv == 'F') t = (bits & 4503599627370495ul) ? "NAN" : "INF";
        for (k = 0; t[k]; k = k + 1) out[n++] = t[k];
        return n;
    }
    expandDouble(v);
    if (conv == 'f' || conv == 'F') {
        int ip;
        if (gLen == 0) gDp = 1;
        roundDigits(gDp + prec);
        if (gLen == 0) {
            /* rounded to nothing: zero */
            out[n++] = '0';
            if (prec > 0) out[n++] = '.';
            for (k = 0; k < prec; k = k + 1) out[n++] = '0';
            return n;
        }
        if (gDp <= 0) out[n++] = '0';
        for (ip = 0; ip < gDp; ip = ip + 1) out[n++] = ip < gLen ? gDig[ip] : '0';
        if (prec > 0) out[n++] = '.';
        for (k = 0; k < prec; k = k + 1) {
            int at = gDp + k;
            out[n++] = (at >= 0 && at < gLen) ? gDig[at] : '0';
        }
        return n;
    }
    {
        int ex;
        if (gLen == 0) {
            gDig[0] = '0';
            gLen = 1;
            gDp = 1;
        }
        roundDigits(prec + 1);
        ex = gDig[0] == '0' ? 0 : gDp - 1;
        out[n++] = gDig[0];
        if (prec > 0) out[n++] = '.';
        for (k = 1; k <= prec; k = k + 1) out[n++] = k < gLen ? gDig[k] : '0';
        out[n++] = conv == 'E' ? 'E' : 'e';
        if (ex < 0) {
            out[n++] = '-';
            ex = -ex;
        } else {
            out[n++] = '+';
        }
        if (ex >= 100) out[n++] = (char)('0' + ex / 100);
        out[n++] = (char)('0' + (ex / 10) % 10);
        out[n++] = (char)('0' + ex % 10);
        return n;
    }
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
                int plus;
                int prec;
                plus = 0;
                prec = -1;
                while (fmt[i] == '-' || fmt[i] == '0' || fmt[i] == '+' || fmt[i] == ' ') {
                    if (fmt[i] == '-') left = 1;
                    if (fmt[i] == '0') zero = 1;
                    if (fmt[i] == '+') plus = 1;
                    i = i + 1;
                }
                while (fmt[i] >= '0' && fmt[i] <= '9') {
                    width = width * 10 + (fmt[i] - '0');
                    i = i + 1;
                }
                if (fmt[i] == '.') {
                    i = i + 1;
                    prec = 0;
                    while (fmt[i] >= '0' && fmt[i] <= '9') {
                        prec = prec * 10 + (fmt[i] - '0');
                        i = i + 1;
                    }
                }
                while (fmt[i] == 'l' || fmt[i] == 'z') {
                    islong = 1;
                    i = i + 1;
                }
                if (fmt[i] == 'd' || fmt[i] == 'u' || fmt[i] == 'x') {
                    char buf[32];
                    int len;
                    int k;
                    unsigned long u;
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
                            u = (unsigned long)0 - (unsigned long)v;
                        } else {
                            u = (unsigned long)v;
                        }
                    } else if (islong) {
                        u = (unsigned long)va_arg(ap, long);
                    } else {
                        u = (unsigned long)va_arg(ap, unsigned);
                    }
                    len = 0;
                    if (u == 0) {
                        buf[0] = '0';
                        len = 1;
                    }
                    while (u != 0) {
                        buf[len] = digits[u % (unsigned long)base];
                        len = len + 1;
                        u = u / (unsigned long)base;
                    }
                    if (!neg && plus && fmt[i] == 'd') {
                        n = n + p_char('+');
                        width = width - 1;
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
                } else if (fmt[i] == 'f' || fmt[i] == 'F' || fmt[i] == 'e' || fmt[i] == 'E' || fmt[i] == 'g' ||
                           fmt[i] == 'G') {
                    char nb[1300];
                    int len;
                    int k;
                    int start;
                    len = fmtDouble(va_arg(ap, double), fmt[i], prec < 0 ? 6 : prec, plus, nb);
                    if (len > 0 && (nb[len - 1] == 'n' || nb[len - 1] == 'f' || nb[len - 1] == 'N' || nb[len - 1] == 'F'))
                        zero = 0; /* inf and nan are padded with spaces */
                    k = width - len;
                    if (k < 0) k = 0;
                    start = 0;
                    if (zero && !left && len > 0 && (nb[0] == '-' || nb[0] == '+')) {
                        n = n + p_char(nb[0]);
                        start = 1;
                    }
                    if (!left)
                        while (k > 0) {
                            n = n + p_char(zero ? '0' : ' ');
                            k = k - 1;
                        }
                    while (start < len) {
                        n = n + p_char(nb[start]);
                        start = start + 1;
                    }
                    if (left)
                        while (k > 0) {
                            n = n + p_char(' ');
                            k = k - 1;
                        }
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
