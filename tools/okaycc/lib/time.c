/* Time for <ctime> and <chrono> (ISO C 7.27; ISO/IEC 14882 [time.clock]). Our own code on the Linux
 * system call clock_gettime 228 (x86-64 table; CLOCK_REALTIME 0, CLOCK_MONOTONIC 1, and
 * CLOCK_PROCESS_CPUTIME_ID 2 for clock()). There is no time zone database: local time is UTC, as
 * with TZ unset. The calendar arithmetic is the proleptic Gregorian calendar of ISO C 7.27.1 p4
 * (struct tm), done year by year and month by month. */
long syscall(long n, long a, long b, long c);
int snprintf(char *buf, unsigned long n, char *fmt, ...);

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
    long tm_gmtoff;
    char *tm_zone;
};

/* Nanoseconds on the clock 'clk' (0 realtime, 1 monotonic, 2 process CPU time). */
long __okay_clock_ns(int clk) {
    long ts[2];
    ts[0] = 0;
    ts[1] = 0;
    if (syscall(228, (long)clk, (long)ts, 0) != 0) return 0;
    return ts[0] * 1000000000 + ts[1];
}

long time(long *out) {
    long t;
    t = __okay_clock_ns(0) / 1000000000;
    if (out) *out = t;
    return t;
}
long clock(void) { return __okay_clock_ns(2) / 1000; } /* CLOCKS_PER_SEC is 1000000 */
double difftime(long a, long b) { return (double)(a - b); }

static int leap(long y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
static int monthDays(int m, long y) {
    if (m == 1) return leap(y) ? 29 : 28;
    if (m == 3 || m == 5 || m == 8 || m == 10) return 30;
    return 31;
}

static char gUtc[4] = {'U', 'T', 'C', 0};

struct tm *gmtime_r(long *tp, struct tm *out) {
    long t;
    long days;
    long secs;
    long y;
    int m;
    t = *tp;
    days = t / 86400;
    secs = t % 86400;
    if (secs < 0) {
        secs = secs + 86400;
        days = days - 1;
    }
    out->tm_hour = (int)(secs / 3600);
    out->tm_min = (int)(secs % 3600 / 60);
    out->tm_sec = (int)(secs % 60);
    /* 1970-01-01 was a Thursday (4). */
    out->tm_wday = (int)((days % 7 + 11) % 7);
    y = 1970;
    while (days < 0) {
        y = y - 1;
        days = days + (leap(y) ? 366 : 365);
    }
    while (days >= (leap(y) ? 366 : 365)) {
        days = days - (leap(y) ? 366 : 365);
        y = y + 1;
    }
    out->tm_year = (int)(y - 1900);
    out->tm_yday = (int)days;
    m = 0;
    while (days >= monthDays(m, y)) {
        days = days - monthDays(m, y);
        m = m + 1;
    }
    out->tm_mon = m;
    out->tm_mday = (int)days + 1;
    out->tm_isdst = 0;
    out->tm_gmtoff = 0;
    out->tm_zone = gUtc;
    return out;
}
struct tm *localtime_r(long *tp, struct tm *out) { return gmtime_r(tp, out); }
static struct tm gTm;
struct tm *gmtime(long *tp) { return gmtime_r(tp, &gTm); }
struct tm *localtime(long *tp) { return gmtime_r(tp, &gTm); }

/* mktime (7.27.2.3): the fields may be out of range; they are normalized. */
long mktime(struct tm *t) {
    long y;
    long mon;
    long days;
    long total;
    int m;
    y = (long)t->tm_year + 1900;
    mon = (long)t->tm_mon;
    y = y + mon / 12;
    mon = mon % 12;
    if (mon < 0) {
        mon = mon + 12;
        y = y - 1;
    }
    days = 0;
    if (y >= 1970) {
        long k;
        for (k = 1970; k < y; k = k + 1) days = days + (leap(k) ? 366 : 365);
    } else {
        long k;
        for (k = y; k < 1970; k = k + 1) days = days - (leap(k) ? 366 : 365);
    }
    for (m = 0; m < mon; m = m + 1) days = days + monthDays(m, y);
    days = days + (long)t->tm_mday - 1;
    total = days * 86400 + (long)t->tm_hour * 3600 + (long)t->tm_min * 60 + (long)t->tm_sec;
    gmtime_r(&total, t);
    return total;
}
long timegm(struct tm *t) { return mktime(t); }

static char *dayName(int d) {
    if (d == 0) return "Sunday";
    if (d == 1) return "Monday";
    if (d == 2) return "Tuesday";
    if (d == 3) return "Wednesday";
    if (d == 4) return "Thursday";
    if (d == 5) return "Friday";
    return "Saturday";
}
static char *monName(int m) {
    if (m == 0) return "January";
    if (m == 1) return "February";
    if (m == 2) return "March";
    if (m == 3) return "April";
    if (m == 4) return "May";
    if (m == 5) return "June";
    if (m == 6) return "July";
    if (m == 7) return "August";
    if (m == 8) return "September";
    if (m == 9) return "October";
    if (m == 10) return "November";
    return "December";
}

/* strftime (7.27.3.5) in the "C" locale: %a %A %b %h %B %c %C %d %D %e %F %H %I %j %m %M %n %p %r %R
 * %S %t %T %u %w %x %X %y %Y %z %Z %%. Returns 0 when the result does not fit. */
unsigned long strftime(char *s, unsigned long max, char *fmt, struct tm *t) {
    unsigned long n;
    int i;
    char tmp[64];
    char *add;
    int len;
    n = 0;
    for (i = 0; fmt[i] != 0; i = i + 1) {
        int k;
        add = tmp;
        tmp[0] = 0;
        if (fmt[i] != '%') {
            tmp[0] = fmt[i];
            tmp[1] = 0;
        } else {
            char c;
            i = i + 1;
            c = fmt[i];
            if (c == 0) break;
            if (c == 'a') snprintf(tmp, 64, "%.3s", dayName(t->tm_wday % 7));
            else if (c == 'A') add = dayName(t->tm_wday % 7);
            else if (c == 'b' || c == 'h') snprintf(tmp, 64, "%.3s", monName(t->tm_mon % 12));
            else if (c == 'B') add = monName(t->tm_mon % 12);
            else if (c == 'c')
                snprintf(tmp, 64, "%.3s %.3s %2d %02d:%02d:%02d %d", dayName(t->tm_wday % 7), monName(t->tm_mon % 12),
                         t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec, t->tm_year + 1900);
            else if (c == 'C') snprintf(tmp, 64, "%02d", (t->tm_year + 1900) / 100);
            else if (c == 'd') snprintf(tmp, 64, "%02d", t->tm_mday);
            else if (c == 'D' || c == 'x') snprintf(tmp, 64, "%02d/%02d/%02d", t->tm_mon + 1, t->tm_mday, (t->tm_year + 1900) % 100);
            else if (c == 'e') snprintf(tmp, 64, "%2d", t->tm_mday);
            else if (c == 'F') snprintf(tmp, 64, "%d-%02d-%02d", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
            else if (c == 'H') snprintf(tmp, 64, "%02d", t->tm_hour);
            else if (c == 'I') snprintf(tmp, 64, "%02d", t->tm_hour % 12 == 0 ? 12 : t->tm_hour % 12);
            else if (c == 'j') snprintf(tmp, 64, "%03d", t->tm_yday + 1);
            else if (c == 'm') snprintf(tmp, 64, "%02d", t->tm_mon + 1);
            else if (c == 'M') snprintf(tmp, 64, "%02d", t->tm_min);
            else if (c == 'n') add = "\n";
            else if (c == 'p') add = t->tm_hour < 12 ? "AM" : "PM";
            else if (c == 'r')
                snprintf(tmp, 64, "%02d:%02d:%02d %s", t->tm_hour % 12 == 0 ? 12 : t->tm_hour % 12, t->tm_min, t->tm_sec,
                         t->tm_hour < 12 ? "AM" : "PM");
            else if (c == 'R') snprintf(tmp, 64, "%02d:%02d", t->tm_hour, t->tm_min);
            else if (c == 'S') snprintf(tmp, 64, "%02d", t->tm_sec);
            else if (c == 't') add = "\t";
            else if (c == 'T' || c == 'X') snprintf(tmp, 64, "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
            else if (c == 'u') snprintf(tmp, 64, "%d", t->tm_wday == 0 ? 7 : t->tm_wday);
            else if (c == 'w') snprintf(tmp, 64, "%d", t->tm_wday);
            else if (c == 'y') snprintf(tmp, 64, "%02d", (t->tm_year + 1900) % 100);
            else if (c == 'Y') snprintf(tmp, 64, "%d", t->tm_year + 1900);
            else if (c == 'z') add = "+0000";
            else if (c == 'Z') add = gUtc;
            else if (c == '%') add = "%";
            else return 0;
        }
        len = 0;
        while (add[len] != 0) len = len + 1;
        if (n + (unsigned long)len + 1 > max) return 0;
        for (k = 0; k < len; k = k + 1) s[n + (unsigned long)k] = add[k];
        n = n + (unsigned long)len;
    }
    if (n + 1 > max) return 0;
    s[n] = 0;
    return n;
}
