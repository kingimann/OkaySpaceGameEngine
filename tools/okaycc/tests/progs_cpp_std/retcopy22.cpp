// return of an object the function does not own (a member, *this, a by-value parameter, a global,
// a reference) copy-constructs the result ([stmt.return] p2). Before round 22 it was copied bytewise:
// the caller's std::string shared the member's buffer and read it after it was freed ("[]" or garbage
// here instead of the path).
#include <cstdio>
#include <string>
#include <vector>

struct P {
    std::string s;
    P(const char *x) : s(x) {}
    P(const std::string &x) : s(x) {}
    P norm() const { return P(s + "/n"); }
    std::string str() const { return s; }
    P self() const { return *this; }
};
static std::string gName = "global-name-long-enough-to-allocate";
static std::string byValue(std::string v) { return v; }
static std::string viaRef(const std::string &r) { return r; }
static std::string global() { return gName; }
static std::string local() {
    std::string x = "local-string-that-is-long";
    return x;
}

int main() {
    std::string pad = "abcdefghijklmnopqrstuvwxyz";
    std::vector<std::string> keep;
    keep.push_back(pad);
    P j("/tmp/okaycc_retcopy22/deep");
    std::string a = j.norm().str();
    std::string b = j.self().norm().str();
    std::string c = byValue(pad + "!");
    std::string d = viaRef(pad);
    std::string e = global();
    std::string f = local();
    keep.push_back(a);
    keep.push_back(c);
    std::printf("[%s] [%s] [%s] [%s] [%s] [%s] %d\n", a.c_str(), b.c_str(), c.c_str(), d.c_str(), e.c_str(),
                f.c_str(), (int)keep.size());
    return 0;
}
