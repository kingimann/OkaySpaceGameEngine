// Our own <chrono>, <ratio> and <ctime> (lib/time.c). Clock readings vary between runs, so only
// their order and the arithmetic on fixed values are printed.
#include <chrono>
#include <cstdio>
#include <ctime>

static double msBetween(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

int main() {
    using namespace std::chrono;
    nanoseconds a(1500000000), b(250000000);
    nanoseconds c = a - b;
    duration<double> s = c;
    duration<double, std::milli> ms(c);
    milliseconds whole = duration_cast<milliseconds>(c);
    seconds secs = duration_cast<seconds>(nanoseconds(-2500000000L));
    std::printf("%ld %g %g %ld %ld\n", (long)c.count(), s.count(), ms.count(), (long)whole.count(), (long)secs.count());
    minutes m(3);
    seconds inSec = m;
    std::printf("%ld %d %d %d\n", (long)inSec.count(), inSec == m, milliseconds(1500) > seconds(1),
                microseconds(999) < milliseconds(1));
    milliseconds acc(0);
    for (int i = 0; i < 4; ++i) acc += milliseconds(250);
    std::printf("%ld %ld\n", (long)acc.count(), (long)duration_cast<seconds>(acc).count());

    steady_clock::time_point t0 = steady_clock::now();
    volatile long spin = 0;
    for (int i = 0; i < 100000; ++i) spin = spin + i;
    steady_clock::time_point t1 = steady_clock::now();
    std::printf("%d %d\n", t1 >= t0, msBetween(t0, t1) >= 0.0);
    auto since = (t1 - t0).count();
    std::printf("%d\n", since >= 0);

    system_clock::time_point now = system_clock::now();
    std::time_t tt = system_clock::to_time_t(now);
    std::time_t direct = std::time(nullptr);
    std::printf("%d\n", direct - tt <= 1 && tt - direct <= 1);
    system_clock::time_point fixed = system_clock::from_time_t(1700000000);
    std::time_t back = system_clock::to_time_t(fixed);
    std::tm parts{};
    gmtime_r(&back, &parts);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %A %B %j %p", &parts);
    std::printf("%s\n", buf);
    std::tm leapDay{};
    leapDay.tm_year = 124;
    leapDay.tm_mon = 1;
    leapDay.tm_mday = 29;
    leapDay.tm_hour = 12;
    std::time_t ld = timegm(&leapDay);
    std::printf("%ld %d %d\n", (long)ld, leapDay.tm_wday, leapDay.tm_yday);
    std::time_t past = -86400L * 366;
    std::tm *old = std::gmtime(&past);
    std::printf("%d %d %d\n", old->tm_year, old->tm_mon, old->tm_mday);
    std::printf("%g\n", std::difftime(100, 40));
    return 0;
}
