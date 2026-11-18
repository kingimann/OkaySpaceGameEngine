/* sscanf for okay-ms programs, written from the conversion description in ISO/IEC 9899 7.21.6.2. Supported conversions: %d %i %u
 * %x %ld %lld %lu %f %lf %e %g (stored as float, double for l) %s %c %n and %%; a width is accepted for %s and the integers; %*
 * skips the value. White space in the format matches any white space. Floating point values are read as digits, a point, digits
 * and an optional exponent, with the value built by repeated multiplication (not correctly rounded in every case). Returns the
 * number of values stored, or -1 when the input ends before the first conversion. */
typedef char *va_list;

static int is_space(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
static int is_digit(int c) { return c >= '0' && c <= '9'; }

static double power10(int e) {
    double r = 1.0;
    double b = 10.0;
    if (e < 0) {
        e = -e;
        while (e > 0) {
            if (e & 1) r *= b;
            b *= b;
            e >>= 1;
        }
        return 1.0 / r;
    }
    while (e > 0) {
        if (e & 1) r *= b;
        b *= b;
        e >>= 1;
    }
    return r;
}

int sscanf(const char *input, const char *fmt, ...) {
    va_list ap;
    const char *s = input;
    int stored = 0;
    int converted = 0;
    va_start(ap, fmt);
    while (*fmt != 0) {
        if (is_space(*fmt)) {
            while (is_space(*s)) ++s;
            ++fmt;
            continue;
        }
        if (*fmt != '%') {
            if (*s != *fmt) break;
            ++s;
            ++fmt;
            continue;
        }
        ++fmt;
        {
            int skip = 0;
            int width = 0;
            int longs = 0;
            char conv;
            if (*fmt == '*') {
                skip = 1;
                ++fmt;
            }
            while (is_digit(*fmt)) {
                width = width * 10 + (*fmt - '0');
                ++fmt;
            }
            while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z') {
                if (*fmt == 'l') ++longs;
                ++fmt;
            }
            conv = *fmt;
            if (conv == 0) break;
            ++fmt;
            if (conv == '%') {
                if (*s != '%') break;
                ++s;
                continue;
            }
            if (conv == 'n') {
                if (!skip) *va_arg(ap, int *) = (int)(s - input);
                continue;
            }
            if (conv != 'c') while (is_space(*s)) ++s;
            if (*s == 0) {
                va_end(ap);
                return converted == 0 && stored == 0 ? -1 : stored;
            }
            if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'x') {
                long long value = 0;
                int neg = 0;
                int digits = 0;
                int base = conv == 'x' ? 16 : 10;
                int w = width == 0 ? 1000000 : width;
                if ((*s == '-' || *s == '+') && w > 0) {
                    neg = *s == '-';
                    ++s;
                    --w;
                }
                if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
                while (w > 0) {
                    int d;
                    if (is_digit(*s)) d = *s - '0';
                    else if (base == 16 && *s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
                    else if (base == 16 && *s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
                    else break;
                    value = value * base + d;
                    ++digits;
                    ++s;
                    --w;
                }
                if (digits == 0) break;
                if (neg) value = -value;
                if (!skip) {
                    if (longs >= 2) *va_arg(ap, long long *) = value;
                    else *va_arg(ap, int *) = (int)value;
                    ++stored;
                }
                ++converted;
            } else if (conv == 'f' || conv == 'e' || conv == 'g' || conv == 'E' || conv == 'G') {
                double value = 0.0;
                int neg = 0;
                int digits = 0;
                int exp10 = 0;
                if (*s == '-' || *s == '+') {
                    neg = *s == '-';
                    ++s;
                }
                while (is_digit(*s)) {
                    value = value * 10.0 + (*s - '0');
                    ++digits;
                    ++s;
                }
                if (*s == '.') {
                    ++s;
                    while (is_digit(*s)) {
                        value = value * 10.0 + (*s - '0');
                        --exp10;
                        ++digits;
                        ++s;
                    }
                }
                if (digits == 0) break;
                if (*s == 'e' || *s == 'E') {
                    const char *save = s;
                    int eneg = 0;
                    int e = 0;
                    int edigits = 0;
                    ++s;
                    if (*s == '-' || *s == '+') {
                        eneg = *s == '-';
                        ++s;
                    }
                    while (is_digit(*s)) {
                        e = e * 10 + (*s - '0');
                        ++edigits;
                        ++s;
                    }
                    if (edigits == 0) s = save;
                    else exp10 += eneg ? -e : e;
                }
                value *= power10(exp10);
                if (neg) value = -value;
                if (!skip) {
                    if (longs >= 1) *va_arg(ap, double *) = value;
                    else *va_arg(ap, float *) = (float)value;
                    ++stored;
                }
                ++converted;
            } else if (conv == 's') {
                char *dst = skip ? 0 : va_arg(ap, char *);
                int w = width == 0 ? 1000000 : width;
                int n = 0;
                while (*s != 0 && !is_space(*s) && w > 0) {
                    if (dst != 0) dst[n] = *s;
                    ++n;
                    ++s;
                    --w;
                }
                if (n == 0) break;
                if (dst != 0) {
                    dst[n] = 0;
                    ++stored;
                }
                ++converted;
            } else if (conv == 'c') {
                char *dst = skip ? 0 : va_arg(ap, char *);
                if (*s == 0) break;
                if (dst != 0) {
                    *dst = *s;
                    ++stored;
                }
                ++s;
                ++converted;
            } else {
                break;
            }
        }
    }
    va_end(ap);
    return stored;
}
