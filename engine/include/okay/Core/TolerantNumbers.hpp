#pragma once
// Number reading that does not give up on "nan" and "inf".
//
// The standard stream reader sets failbit on those words, so one non-finite value in a text file stops the
// whole rest of the read. A stream imbued with TolerantLocale() reads them as numbers (and "nan(payload)",
// "-inf", "infinity"); every other number reads exactly as with the classic locale. The scene loader and the
// .okaymat reader share it. Written from the C++ std::num_get contract (do_get for float and double).
#include <locale>
#include <string>

namespace okay {

/// The classic locale, with a number reader that also accepts nan / inf. Lives to the end of the process.
const std::locale& TolerantLocale();

/// Read one float from the whole of `text` (spaces around it allowed) with TolerantLocale(). False, and `out`
/// untouched, when `text` is not exactly one number. nan and inf are numbers here; the caller decides on them.
bool ParseTolerantFloat(const std::string& text, float& out);

} // namespace okay
