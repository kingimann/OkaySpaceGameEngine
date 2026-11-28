/* printf and snprintf share one formatter.
 * Written from the fprintf conversion description in ISO/IEC 9899:
 * flags - + space 0, a decimal width, an optional precision, and d u x c s f.
 * The digits and the padding are worked out here.
 */

typedef char *va_list;

__declspec(dllimport) void *GetStdHandle(unsigned int n);
__declspec(dllimport) int WriteFile(void *h, void *buf, unsigned int n, unsigned int *wrote, void *ov);

int format_out(char *buf, unsigned int cap, char *fmt, va_list ap) {
    unsigned int n;
    n = 0;
    if (cap == 0) return 0;
    while (fmt[0] != 0) {
        if (fmt[0] != '%') {
            if (n + 1 < cap) {
                buf[n] = fmt[0];
                n = n + 1;
            }
            fmt = fmt + 1;
        } else {
            int left;
            int plus;
            int zero;
            int space;
            int width;
            int prec;
            int has_prec;
            char spec;
            char tmp[64];
            char rev[32];
            int tn;
            int ri;
            int sign;
            int pad;
            int i;
            char fill;
            unsigned long long u;
            unsigned int base;
            unsigned int digit;
            long long v;
            int wide;
            int upper;
            char *s;
            char *src;
            double fval;
            int fwhole;
            int fi;
            int fdigit;
            double ffrac;
            left = 0;
            plus = 0;
            zero = 0;
            space = 0;
            width = 0;
            prec = 6;
            has_prec = 0;
            wide = 0;
            upper = 0;
            src = tmp;
            fmt = fmt + 1;
            if (fmt[0] == '%') {
                if (n + 1 < cap) {
                    buf[n] = '%';
                    n = n + 1;
                }
                fmt = fmt + 1;
            } else {
                while (fmt[0] == '-' || fmt[0] == '+' || fmt[0] == '0' || fmt[0] == ' ') {
                    if (fmt[0] == '-') left = 1;
                    if (fmt[0] == '+') plus = 1;
                    if (fmt[0] == '0') zero = 1;
                    if (fmt[0] == ' ') space = 1;
                    fmt = fmt + 1;
                }
                while (fmt[0] >= '0' && fmt[0] <= '9') {
                    width = width * 10 + (fmt[0] - '0');
                    fmt = fmt + 1;
                }
                if (fmt[0] == '.') {
                    has_prec = 1;
                    prec = 0;
                    fmt = fmt + 1;
                    while (fmt[0] >= '0' && fmt[0] <= '9') {
                        prec = prec * 10 + (fmt[0] - '0');
                        fmt = fmt + 1;
                    }
                }
                while (fmt[0] == 'l' || fmt[0] == 'h' || fmt[0] == 'z' || fmt[0] == 'j' || fmt[0] == 't') {
                    /* ll, z, j and t are 64 bits wide; l and h are 32 bits or less on Windows. */
                    if ((fmt[0] == 'l' && fmt[1] == 'l') || fmt[0] == 'z' || fmt[0] == 'j' || fmt[0] == 't') wide = 1;
                    fmt = fmt + 1;
                }
                spec = fmt[0];
                if (spec == 0) {
                    buf[n] = 0;
                    return n;
                }
                fmt = fmt + 1;
                tn = 0;
                sign = 0;
                base = 0;
                u = 0;
                if (spec == 'd' || spec == 'i') {
                    if (wide) v = va_arg(ap, long long);
                    else v = va_arg(ap, int);
                    if (v < 0) {
                        sign = '-';
                        v = -v;
                    } else if (plus) sign = '+';
                    else if (space) sign = ' ';
                    u = v;
                    base = 10;
                } else if (spec == 'u') {
                    if (wide) u = va_arg(ap, unsigned long long);
                    else u = va_arg(ap, unsigned int);
                    base = 10;
                } else if (spec == 'x' || spec == 'X') {
                    if (wide) u = va_arg(ap, unsigned long long);
                    else u = va_arg(ap, unsigned int);
                    base = 16;
                    if (spec == 'X') upper = 1;
                } else if (spec == 'c') {
                    v = va_arg(ap, int);
                    tmp[0] = v;
                    tn = 1;
                } else if (spec == 's') {
                    /* The text is written from where it is, at any length (Round 35; before, it went through the
                       64-byte tmp and was cut at 60 characters). */
                    s = va_arg(ap, char *);
                    src = s;
                    while (s[tn] != 0 && (has_prec == 0 || tn < prec)) tn = tn + 1;
                } else if (spec == 'f') {
                    fval = va_arg(ap, double);
                    if (fval < 0) {
                        sign = '-';
                        fval = -fval;
                    } else if (plus) sign = '+';
                    else if (space) sign = ' ';
                    fwhole = (int)fval;
                    u = fwhole;
                    base = 10;
                    if (prec < 0) prec = 0;
                    if (prec > 12) prec = 12;
                } else {
                    tmp[0] = spec;
                    tn = 1;
                }
                if (base != 0) {
                    ri = 0;
                    if (u == 0) {
                        rev[0] = '0';
                        ri = 1;
                    }
                    while (u != 0 && ri < 30) {
                        digit = u % base;
                        if (digit < 10) rev[ri] = '0' + digit;
                        else if (upper) rev[ri] = 'A' + digit - 10;
                        else rev[ri] = 'a' + digit - 10;
                        ri = ri + 1;
                        u = u / base;
                    }
                    while (ri > 0 && tn < 40) {
                        ri = ri - 1;
                        tmp[tn] = rev[ri];
                        tn = tn + 1;
                    }
                }
                if (spec == 'f' && prec > 0 && tn < 60) {
                    tmp[tn] = '.';
                    tn = tn + 1;
                    ffrac = fval - (double)fwhole;
                    fi = 0;
                    while (fi < prec && tn < 60) {
                        ffrac = ffrac * 10.0;
                        fdigit = (int)ffrac;
                        tmp[tn] = '0' + fdigit;
                        tn = tn + 1;
                        ffrac = ffrac - (double)fdigit;
                        fi = fi + 1;
                    }
                }
                pad = width - tn;
                if (sign != 0) pad = pad - 1;
                if (pad < 0) pad = 0;
                if (left) fill = ' ';
                else if (zero) fill = '0';
                else fill = ' ';
                if (left == 0) {
                    if (zero && sign != 0 && n + 1 < cap) {
                        buf[n] = sign;
                        n = n + 1;
                        sign = 0;
                    }
                    while (pad > 0 && n + 1 < cap) {
                        buf[n] = fill;
                        n = n + 1;
                        pad = pad - 1;
                    }
                }
                if (sign != 0 && n + 1 < cap) {
                    buf[n] = sign;
                    n = n + 1;
                }
                i = 0;
                while (i < tn && n + 1 < cap) {
                    buf[n] = src[i];
                    n = n + 1;
                    i = i + 1;
                }
                if (left) {
                    while (pad > 0 && n + 1 < cap) {
                        buf[n] = ' ';
                        n = n + 1;
                        pad = pad - 1;
                    }
                }
            }
        }
    }
    if (n < cap) buf[n] = 0;
    else buf[cap - 1] = 0;
    return n;
}

int printf(char *fmt, ...) {
    char out[1024];
    unsigned int wrote;
    int count;
    va_list ap;
    va_start(ap, fmt);
    count = format_out(out, 1024, fmt, ap);
    va_end(ap);
    WriteFile(GetStdHandle(0xFFFFFFF5), out, count, &wrote, 0);
    return count;
}

int snprintf(char *buf, unsigned int cap, char *fmt, ...) {
    int count;
    va_list ap;
    va_start(ap, fmt);
    count = format_out(buf, cap, fmt, ap);
    va_end(ap);
    return count;
}

int puts(char *s) { return printf("%s\n", s); }

/* The C runtime's three standard streams (Round 36), for programs that write to stderr or stdout through a FILE:
 * __acrt_iob_func(i) gives stream i (0 stdin, 1 stdout, 2 stderr; the UCRT entry point <cstdio> names), and fprintf,
 * fputs, fputc, fwrite and fflush write to stdout and stderr through WriteFile on the process's standard handles
 * (STD_OUTPUT_HANDLE -11, STD_ERROR_HANDLE -12, Microsoft's GetStdHandle documentation). Own minimal version: these
 * streams only, no buffering (fflush does nothing), and nothing is read from stdin. Written from ISO/IEC 9899 7.21.
 */
unsigned int strlen(char *s);
static int okay_streams[3] = {0, 1, 2};

void *__acrt_iob_func(unsigned int index) { return index < 3 ? (void *)&okay_streams[index] : 0; }

static void *okay_stream_handle(void *stream) {
    int which = stream ? *(int *)stream : 1;
    return GetStdHandle(which == 2 ? 0xFFFFFFF4 : 0xFFFFFFF5);
}

int fprintf(void *stream, char *fmt, ...) {
    char out[1024];
    unsigned int wrote;
    int count;
    va_list ap;
    va_start(ap, fmt);
    count = format_out(out, 1024, fmt, ap);
    va_end(ap);
    WriteFile(okay_stream_handle(stream), out, count, &wrote, 0);
    return count;
}

unsigned long long fwrite(void *data, unsigned long long size, unsigned long long count, void *stream) {
    unsigned int wrote = 0;
    if (size == 0 || count == 0) return 0;
    WriteFile(okay_stream_handle(stream), data, (unsigned int)(size * count), &wrote, 0);
    return wrote / size;
}

int fputs(char *s, void *stream) {
    unsigned int wrote;
    WriteFile(okay_stream_handle(stream), s, strlen(s), &wrote, 0);
    return 0;
}

int fputc(int c, void *stream) {
    char one = (char)c;
    unsigned int wrote;
    WriteFile(okay_stream_handle(stream), &one, 1, &wrote, 0);
    return c & 255;
}

int fflush(void *stream) { return 0; }

