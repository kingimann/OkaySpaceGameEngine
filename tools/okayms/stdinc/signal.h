// okay-ms own <signal.h>: the C name of <csignal> (ISO/IEC 9899 7.14), with signal and raise also at global scope, as
// the C header has them (Round 30).
#pragma once
#include <csignal>
using std::raise;
using std::signal;
using std::sig_atomic_t;
