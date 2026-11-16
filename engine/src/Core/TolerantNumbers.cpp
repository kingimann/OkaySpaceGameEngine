#include "okay/Core/TolerantNumbers.hpp"
#include <cctype>
#include <ios>
#include <limits>
#include <sstream>

namespace okay {

namespace {

// Reads "nan" / "inf" (what the writer prints for a non-finite float) as well as numbers. The
// standard stream reader fails on those words, which made a scene saved with one NaN in it
// (a physics blow-up, then an autosave) a file that could not be opened at all. Other values
// parse exactly as before: the sign is taken here and the digits go to the standard reader.
class TolerantNumGet : public std::num_get<char> {
public:
    explicit TolerantNumGet(std::size_t refs) : std::num_get<char>(refs) {}
protected:
    using std::num_get<char>::do_get;
    iter_type do_get(iter_type in, iter_type end, std::ios_base& str, std::ios_base::iostate& err, float& v) const override {
        return Get(in, end, str, err, v);
    }
    iter_type do_get(iter_type in, iter_type end, std::ios_base& str, std::ios_base::iostate& err, double& v) const override {
        return Get(in, end, str, err, v);
    }
private:
    template <class F>
    iter_type Get(iter_type in, iter_type end, std::ios_base& str, std::ios_base::iostate& err, F& v) const {
        bool neg = false;
        if (in != end && (*in == '-' || *in == '+')) {
            neg = (*in == '-'); ++in;
            if (in != end && (*in == '-' || *in == '+')) { v = 0; err |= std::ios_base::failbit; return in; }   // "--5" is not a number
        }
        if (in != end && (*in == 'n' || *in == 'N' || *in == 'i' || *in == 'I')) {
            std::string word;
            while (in != end && std::isalpha(static_cast<unsigned char>(*in))) { word += static_cast<char>(std::tolower(static_cast<unsigned char>(*in))); ++in; }
            if (in != end && *in == '(') {   // nan(payload)
                while (in != end && *in != ')') ++in;
                if (in != end) ++in;
            }
            if (word == "nan")                        v = std::numeric_limits<F>::quiet_NaN();
            else if (word == "inf" || word == "infinity") v = std::numeric_limits<F>::infinity();
            else { v = 0; err |= std::ios_base::failbit; return in; }
            if (neg) v = -v;
            if (in == end) err |= std::ios_base::eofbit;
            return in;
        }
        iter_type r = std::num_get<char>::do_get(in, end, str, err, v);
        if (neg && !(err & std::ios_base::failbit)) v = -v;
        return r;
    }
};

} // namespace

const std::locale& TolerantLocale() {
    static const std::locale* loc = new std::locale(std::locale::classic(), new TolerantNumGet(0));   // lives to the end of the process, like the writer's
    return *loc;
}

bool ParseTolerantFloat(const std::string& text, float& out) {
    std::istringstream in(text);
    in.imbue(TolerantLocale());
    float v = 0.0f;
    if (!(in >> v)) return false;
    in >> std::ws;
    if (!in.eof()) return false;   // something after the number: "1.5abc" is not a number
    out = v;
    return true;
}

} // namespace okay
