/* Math functions for okay-cc programs: sin, cos, exp, log, log10 and pow, in double and float.
 * Written from ISO C 7.12 (what each function means and its special values), IEEE 754 (round to
 * nearest), Dekker's exact product and Knuth's two-sum (double-double arithmetic, about 106 bits)
 * and the Taylor and atanh series. pi, ln 2 and ln 10 are worked out here from series (Machin's
 * formula, atanh(1/3), log(1.25)), not copied. Each result is the double-double value rounded once
 * to double (or float), so it is the correctly rounded result except in rare cases where the true
 * value lies within about 2^-100 of a halfway point. sin and cos reduce the argument with a 106-bit
 * pi/2: for |x| above 2^20 that loses accuracy (not handled yet). sqrt is not here: it is the
 * SQRTSD instruction (__builtin_sqrt), which IEEE 754 rounds exactly. tan is sin / cos; atan uses
 * the halving identity and its series; atan2, asin and acos are built on it (asin and acos with
 * (1 - x)(1 + x) kept exact); log2 is log x / ln 2. */

typedef struct {
    double hi;
    double lo;
} dd;

static unsigned long bitsOf(double x) { return *(unsigned long *)&x; }
static double fromBits(unsigned long u) { return *(double *)&u; }

static dd mk(double hi, double lo) {
    dd r;
    r.hi = hi;
    r.lo = lo;
    return r;
}
/* a + b exactly as hi + lo (Knuth). */
static dd twoSum(double a, double b) {
    double s = a + b;
    double bb = s - a;
    double err = (a - (s - bb)) + (b - bb);
    return mk(s, err);
}
static dd quickSum(double a, double b) {
    double s = a + b;
    return mk(s, b - (s - a));
}
/* a * b exactly as hi + lo (Dekker: each factor cut into halves with 2^27 + 1). */
static void dekkerHalves(double a, double *hi, double *lo) {
    double c = 134217729.0 * a;
    double h = c - (c - a);
    *hi = h;
    *lo = a - h;
}
static dd twoProd(double a, double b) {
    double p = a * b;
    double ah, al, bh, bl;
    dekkerHalves(a, &ah, &al);
    dekkerHalves(b, &bh, &bl);
    double err = ((ah * bh - p) + ah * bl + al * bh) + al * bl;
    return mk(p, err);
}
static dd ddAdd(dd x, dd y) {
    dd s = twoSum(x.hi, y.hi);
    dd t = twoSum(x.lo, y.lo);
    s.lo = s.lo + t.hi;
    s = quickSum(s.hi, s.lo);
    s.lo = s.lo + t.lo;
    return quickSum(s.hi, s.lo);
}
static dd ddNeg(dd x) { return mk(-x.hi, -x.lo); }
static dd ddSub(dd x, dd y) { return ddAdd(x, ddNeg(y)); }
static dd ddMul(dd x, dd y) {
    dd p = twoProd(x.hi, y.hi);
    p.lo = p.lo + (x.hi * y.lo + x.lo * y.hi);
    return quickSum(p.hi, p.lo);
}
static dd ddMulD(dd x, double y) {
    dd p = twoProd(x.hi, y);
    p.lo = p.lo + x.lo * y;
    return quickSum(p.hi, p.lo);
}
static dd ddDiv(dd x, dd y) {
    double q1 = x.hi / y.hi;
    dd r = ddSub(x, ddMulD(y, q1));
    double q2 = r.hi / y.hi;
    r = ddSub(r, ddMulD(y, q2));
    double q3 = r.hi / y.hi;
    dd q = quickSum(q1, q2);
    return ddAdd(q, mk(q3, 0.0));
}
static dd ddDivD(dd x, double y) { return ddDiv(x, mk(y, 0.0)); }

/* Round hi + lo to float with one rounding: a double rounding through double could land on the
 * wrong side of a float halfway point. */
static float ddToFloat(dd x) {
    float f = (float)x.hi;
    double fd = (double)f;
    if (fd == x.hi || x.lo == 0.0) return f;
    /* the float on the other side of hi */
    float g;
    unsigned int fb = *(unsigned int *)&f;
    if ((x.hi > fd) == (f >= 0.0f)) fb = fb + 1;
    else fb = fb - 1;
    g = *(float *)&fb;
    double mid = (fd + (double)g) * 0.5;
    if (x.hi != mid) return f;
    /* hi is exactly halfway: lo says which way the true value lies */
    if ((x.lo > 0.0) == ((double)g > fd)) return g;
    return f;
}

/* atanh(t) = t + t^3/3 + t^5/5 + ... for |t| <= 1/3, in double-double. */
static dd atanhSeries(dd t) {
    dd t2 = ddMul(t, t);
    dd term = t;
    dd sum = t;
    int k;
    for (k = 3; k < 200; k = k + 2) {
        term = ddMul(term, t2);
        dd add = ddDivD(term, (double)k);
        sum = ddAdd(sum, add);
        if (add.hi == 0.0) break;
        double a = add.hi < 0 ? -add.hi : add.hi;
        double s = sum.hi < 0 ? -sum.hi : sum.hi;
        if (a < s * 1e-34) break;
    }
    return sum;
}
/* atan(1/n) = 1/n - 1/(3 n^3) + 1/(5 n^5) - ... */
static dd atanInv(double n) {
    dd x = ddDivD(mk(1.0, 0.0), n);
    dd x2 = ddMul(x, x);
    dd term = x;
    dd sum = x;
    int k;
    int sign = -1;
    for (k = 3; k < 400; k = k + 2) {
        term = ddMul(term, x2);
        dd add = ddDivD(term, (double)k);
        if (sign < 0) sum = ddSub(sum, add);
        else sum = ddAdd(sum, add);
        sign = -sign;
        if (add.hi < sum.hi * 1e-34) break;
    }
    return sum;
}

static int gReady = 0;
static dd gLn2;
static dd gLn10;
static dd gPiOver2;
static dd logReduced(double m, int e);
static void ready(void) {
    if (gReady) return;
    gReady = 1;
    /* ln 2 = 2 atanh(1/3) */
    gLn2 = ddMulD(atanhSeries(ddDivD(mk(1.0, 0.0), 3.0)), 2.0);
    /* ln 10 = 3 ln 2 + ln 1.25 */
    gLn10 = logReduced(1.25, 3);
    /* pi / 2 = 8 atan(1/5) - 2 atan(1/239) (Machin: pi/4 = 4 atan(1/5) - atan(1/239)) */
    gPiOver2 = ddSub(ddMulD(atanInv(5.0), 8.0), ddMulD(atanInv(239.0), 2.0));
}

/* log(m * 2^e) for m in [1/sqrt2, sqrt2]: e ln2 + 2 atanh((m - 1) / (m + 1)). */
static dd logReduced(double m, int e) {
    dd num = twoSum(m, -1.0);
    dd den = twoSum(m, 1.0);
    dd t = ddDiv(num, den);
    dd l = ddMulD(atanhSeries(t), 2.0);
    if (e != 0) l = ddAdd(ddMulD(gLn2, (double)e), l);
    return l;
}
/* x > 0, finite: log x in double-double. */
static dd logDD(double x) {
    int e = 0;
    unsigned long u = bitsOf(x);
    if (((u >> 52) & 2047) == 0) {
        x = x * 18014398509481984.0; /* 2^54: a subnormal becomes normal */
        e = -54;
        u = bitsOf(x);
    }
    e = e + (int)((u >> 52) & 2047) - 1023;
    double m = fromBits((u & 4503599627370495ul) | 4607182418800017408ul); /* [1, 2) */
    if (m > 1.4142135623730951) {
        m = m * 0.5;
        e = e + 1;
    }
    return logReduced(m, e);
}

static double inf(void) { return fromBits(9218868437227405312ul); }
static double qnan(void) { return fromBits(9221120237041090560ul); }
static int isNan(double x) { return x != x; }

/* 2^k as a double, for -1074 <= k <= 1023 */
static double pow2(int k) {
    if (k > 1023) return inf();
    if (k >= -1022) return fromBits((unsigned long)(k + 1023) << 52);
    if (k < -1074) return 0.0;
    return fromBits(1ul << (k + 1074));
}

/* exp of a double-double t (|t| < 746), rounded to double. */
static double expDD(dd t) {
    if (t.hi > 709.8) return inf();
    if (t.hi < -745.2) return 0.0;
    double kd = t.hi / gLn2.hi;
    long k = (long)(kd < 0 ? kd - 0.5 : kd + 0.5);
    dd r = ddSub(t, ddMulD(gLn2, (double)k));
    /* Taylor series of e^r, |r| <= 0.35 */
    dd sum = mk(1.0, 0.0);
    dd term = mk(1.0, 0.0);
    int n;
    for (n = 1; n < 60; n = n + 1) {
        term = ddDivD(ddMul(term, r), (double)n);
        sum = ddAdd(sum, term);
        double a = term.hi < 0 ? -term.hi : term.hi;
        if (a < 1e-34) break;
    }
    if (k >= -1021 && k <= 1023) {
        double s = pow2((int)k);
        return (sum.hi + sum.lo) * s; /* exact scaling by a power of two */
    }
    /* a result near the ends of the range: scale in two steps (a subnormal result rounds once
     * more here, see the plan) */
    double s1 = pow2((int)(k / 2));
    double s2 = pow2((int)(k - k / 2));
    return (sum.hi + sum.lo) * s1 * s2;
}

/* sin (q even) or cos (q odd) of a reduced r, |r| <= pi/4, as double-double */
static dd sinDD(dd r) {
    dd r2 = ddMul(r, r);
    dd term = r;
    dd sum = r;
    int n;
    for (n = 3; n < 60; n = n + 2) {
        term = ddDivD(ddMul(term, r2), (double)((n - 1) * n));
        term = ddNeg(term);
        sum = ddAdd(sum, term);
        double a = term.hi < 0 ? -term.hi : term.hi;
        if (a < 1e-36) break;
    }
    return sum;
}
static dd cosDD(dd r) {
    dd r2 = ddMul(r, r);
    dd term = mk(1.0, 0.0);
    dd sum = mk(1.0, 0.0);
    int n;
    for (n = 2; n < 60; n = n + 2) {
        term = ddDivD(ddMul(term, r2), (double)((n - 1) * n));
        term = ddNeg(term);
        sum = ddAdd(sum, term);
        double a = term.hi < 0 ? -term.hi : term.hi;
        if (a < 1e-36) break;
    }
    return sum;
}
/* sin (wantCos 0) or cos (wantCos 1) of x as double-double */
static dd trigDD(double x, int wantCos) {
    /* Above 2^62 the quotient no longer fits a long (and the 106-bit pi/2 is far too short for
     * the reduction): no value is given (see the plan). */
    if (x > 4611686018427387904.0 || x < -4611686018427387904.0) return mk(qnan(), 0.0);
    double kd = x / gPiOver2.hi;
    long k = (long)(kd < 0 ? kd - 0.5 : kd + 0.5);
    dd r = mk(x, 0.0);
    if (k != 0) {
        /* r = x - k pi/2 with the product k * (pi/2) kept exact as a sum of double-doubles */
        dd p1 = twoProd((double)k, gPiOver2.hi);
        dd p2 = twoProd((double)k, gPiOver2.lo);
        r = ddSub(r, p1);
        r = ddSub(r, p2);
    }
    int q = (int)(k & 3);
    if (wantCos) q = (q + 1) & 3;
    dd v;
    if (q == 0) v = sinDD(r);
    else if (q == 1) v = cosDD(r);
    else if (q == 2) v = ddNeg(sinDD(r));
    else v = ddNeg(cosDD(r));
    return v;
}

double __okay_sin(double x) {
    if (isNan(x) || x - x != 0) return qnan();
    if (x == 0.0) return x; /* keeps the sign of zero */
    ready();
    dd v = trigDD(x, 0);
    return v.hi + v.lo;
}
double __okay_cos(double x) {
    if (isNan(x) || x - x != 0) return qnan();
    ready();
    dd v = trigDD(x, 1);
    return v.hi + v.lo;
}
double __okay_log(double x) {
    if (isNan(x)) return x;
    if (x < 0) return qnan();
    if (x == 0) return -inf();
    if (x - x != 0) return x;
    ready();
    dd l = logDD(x);
    return l.hi + l.lo;
}
double __okay_log10(double x) {
    if (isNan(x)) return x;
    if (x < 0) return qnan();
    if (x == 0) return -inf();
    if (x - x != 0) return x;
    ready();
    dd l = ddDiv(logDD(x), gLn10);
    return l.hi + l.lo;
}
double __okay_exp(double x) {
    if (isNan(x)) return x;
    if (x - x != 0) return x > 0 ? x : 0.0;
    ready();
    return expDD(mk(x, 0.0));
}
static int isInteger(double y) {
    double a = y < 0 ? -y : y;
    if (a >= 4503599627370496.0) return 1;
    return (double)(long)y == y;
}
static int isOdd(double y) {
    double a = y < 0 ? -y : y;
    if (a >= 9007199254740992.0) return 0;
    long n = (long)y;
    return (n & 1) != 0;
}
double __okay_pow(double x, double y) {
    if (y == 0) return 1.0;
    if (x == 1.0) return 1.0;
    if (isNan(x) || isNan(y)) return qnan();
    double ay = y < 0 ? -y : y;
    if (ay - ay != 0) { /* y is infinite */
        double ax = x < 0 ? -x : x;
        if (ax == 1.0) return 1.0;
        if ((ax > 1.0) == (y > 0)) return inf();
        return 0.0;
    }
    int neg = 0;
    if (x == 0.0) {
        /* pow(+-0, y): 0 or inf, negative for -0 and an odd integer y (C F.10.4.4) */
        double z = y > 0 ? 0.0 : inf();
        if ((bitsOf(x) >> 63) != 0 && isInteger(y) && isOdd(y)) z = -z;
        return z;
    }
    if (x < 0) {
        if (!isInteger(y)) return qnan();
        neg = isOdd(y);
        x = -x;
    }
    if (x - x != 0) { /* x is infinite */
        double z = y > 0 ? inf() : 0.0;
        return neg ? -z : z;
    }
    ready();
    dd l = logDD(x);
    /* y * log x as double-double: (hi + lo) * y */
    dd t = ddMulD(l, y);
    double r = expDD(t);
    return neg ? -r : r;
}

/* sqrt of a double-double (x > 0): the hardware root, then one Newton step in double-double. */
static dd ddSqrt(dd x) {
    if (x.hi <= 0.0) return mk(0.0, 0.0);
    double s = __builtin_sqrt(x.hi);
    dd r = ddSub(x, twoProd(s, s));
    return quickSum(s, r.hi / (2.0 * s));
}
/* atan of a double-double. |x| > 1 uses atan x = pi/2 - atan(1/x); then the halving identity
 * atan x = 2 atan(x / (1 + sqrt(1 + x^2))) three times (|x| <= tan(pi/32)), then the series
 * x - x^3/3 + x^5/5 - ... */
static dd atanDD(dd x) {
    int neg = x.hi < 0;
    if (neg) x = ddNeg(x);
    int inv = x.hi > 1.0;
    if (inv) x = ddDiv(mk(1.0, 0.0), x);
    int halvings = 3;
    int h;
    for (h = 0; h < halvings; h = h + 1) {
        dd root = ddSqrt(ddAdd(mk(1.0, 0.0), ddMul(x, x)));
        x = ddDiv(x, ddAdd(mk(1.0, 0.0), root));
    }
    dd x2 = ddMul(x, x);
    dd term = x;
    dd sum = x;
    int k;
    int sign = -1;
    for (k = 3; k < 200; k = k + 2) {
        term = ddMul(term, x2);
        dd add = ddDivD(term, (double)k);
        if (sign < 0) sum = ddSub(sum, add);
        else sum = ddAdd(sum, add);
        sign = -sign;
        double a = add.hi < 0 ? -add.hi : add.hi;
        if (a < 1e-36) break;
    }
    sum = ddMulD(sum, 8.0); /* 2^halvings */
    if (inv) sum = ddSub(gPiOver2, sum);
    if (neg) return ddNeg(sum);
    return sum;
}
/* atan2 of double-doubles y, x (not both zero), by quadrant. */
static dd atan2DD(dd y, dd x) {
    dd pi = ddMulD(gPiOver2, 2.0);
    if (x.hi == 0.0) {
        if (y.hi > 0) return gPiOver2;
        return ddNeg(gPiOver2);
    }
    dd a = atanDD(ddDiv(y, x));
    if (x.hi > 0) return a;
    if (y.hi >= 0) return ddAdd(a, pi);
    return ddSub(a, pi);
}

double __okay_tan(double x) {
    if (isNan(x) || x - x != 0) return qnan();
    if (x == 0.0) return x;
    ready();
    dd v = ddDiv(trigDD(x, 0), trigDD(x, 1));
    return v.hi + v.lo;
}
double __okay_atan(double x) {
    if (isNan(x)) return x;
    ready();
    if (x - x != 0) return x > 0 ? gPiOver2.hi : -gPiOver2.hi;
    if (x == 0.0) return x;
    dd v = atanDD(mk(x, 0.0));
    return v.hi + v.lo;
}
double __okay_atan2(double y, double x) {
    if (isNan(x) || isNan(y)) return qnan();
    ready();
    int ysign = (bitsOf(y) >> 63) != 0;
    int xsign = (bitsOf(x) >> 63) != 0;
    double pi = gPiOver2.hi * 2.0; /* pi rounded (2 * a double is exact) */
    if (y == 0.0) {
        /* atan2(+-0, x): +-0 for x > 0 or +0, +-pi for x < 0 or -0 (C F.10.1.4) */
        if (xsign) return ysign ? -pi : pi;
        return y;
    }
    if (x == 0.0) return ysign ? -gPiOver2.hi : gPiOver2.hi;
    if (y - y != 0 || x - x != 0) {
        /* an infinite argument */
        double quarter = gPiOver2.hi * 0.5;
        double r;
        if (y - y != 0 && x - x != 0) r = xsign ? 3.0 * quarter : quarter;
        else if (y - y != 0) r = gPiOver2.hi;
        else r = xsign ? pi : 0.0;
        return ysign ? -r : r;
    }
    dd v = atan2DD(mk(y, 0.0), mk(x, 0.0));
    return v.hi + v.lo;
}
/* asin and acos through atan2 with sqrt((1 - x)(1 + x)) kept in double-double. */
static dd oneMinusSq(double x) { return ddMul(twoSum(1.0, -x), twoSum(1.0, x)); }
double __okay_asin(double x) {
    if (isNan(x) || x > 1.0 || x < -1.0) return qnan();
    if (x == 0.0) return x;
    ready();
    dd v = atan2DD(mk(x, 0.0), ddSqrt(oneMinusSq(x)));
    if (x == 1.0) v = gPiOver2;
    if (x == -1.0) v = ddNeg(gPiOver2);
    return v.hi + v.lo;
}
double __okay_acos(double x) {
    if (isNan(x) || x > 1.0 || x < -1.0) return qnan();
    if (x == 1.0) return 0.0;
    ready();
    dd v;
    if (x == -1.0) v = ddMulD(gPiOver2, 2.0);
    else v = atan2DD(ddSqrt(oneMinusSq(x)), mk(x, 0.0));
    return v.hi + v.lo;
}
double __okay_log2(double x) {
    if (isNan(x)) return x;
    if (x < 0) return qnan();
    if (x == 0) return -inf();
    if (x - x != 0) return x;
    ready();
    dd l = ddDiv(logDD(x), gLn2);
    return l.hi + l.lo;
}

/* The float functions: the double-double value rounded once to float. */
float __okay_sinf(float x) {
    double d = (double)x;
    if (isNan(d) || d - d != 0) return (float)qnan();
    if (d == 0.0) return x;
    ready();
    return ddToFloat(trigDD(d, 0));
}
float __okay_cosf(float x) {
    double d = (double)x;
    if (isNan(d) || d - d != 0) return (float)qnan();
    ready();
    return ddToFloat(trigDD(d, 1));
}
float __okay_logf(float x) { return (float)__okay_log((double)x); }
float __okay_log10f(float x) { return (float)__okay_log10((double)x); }
float __okay_expf(float x) { return (float)__okay_exp((double)x); }
float __okay_powf(float x, float y) { return (float)__okay_pow((double)x, (double)y); }
float __okay_tanf(float x) {
    double d = (double)x;
    if (isNan(d) || d - d != 0) return (float)qnan();
    if (d == 0.0) return x;
    ready();
    return ddToFloat(ddDiv(trigDD(d, 0), trigDD(d, 1)));
}
float __okay_atanf(float x) { return (float)__okay_atan((double)x); }
float __okay_atan2f(float y, float x) { return (float)__okay_atan2((double)y, (double)x); }
float __okay_asinf(float x) { return (float)__okay_asin((double)x); }
float __okay_acosf(float x) { return (float)__okay_acos((double)x); }
float __okay_log2f(float x) { return (float)__okay_log2((double)x); }
