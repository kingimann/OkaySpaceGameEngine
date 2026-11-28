// okay-ms own <time.h>: the C name of <ctime> (ISO/IEC 9899 7.27), with the time functions also at global scope, as the
// C header has them (Round 35; 33 files of the plain survey include it through AccountDetail.hpp and AccountService.hpp).
// The POSIX additions (nanosleep, clock_gettime, timegm, struct timespec) are already global in <ctime>.
#pragma once
#include <ctime>
using std::clock;
using std::difftime;
using std::gmtime;
using std::localtime;
using std::mktime;
using std::strftime;
using std::time;
