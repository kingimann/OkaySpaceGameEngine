// c ? derived : base with two lvalues of classes ([expr.cond] p4.1): the derived one is used as
// its base (a TagString derived from std::string beside a std::string, Components/InventoryUI.hpp).
#include <cstdio>
#include <string>
class TagString : public std::string {
public:
    TagString() {}
    TagString& operator=(const std::string& s) {
        std::string::operator=(s);
        return *this;
    }
};
struct Obj {
    std::string name = "crate";
    TagString tag;
};
int main() {
    Obj o;
    std::string a = o.tag.empty() ? o.name : o.tag;
    o.tag = "loot";
    std::string b = o.tag.empty() ? o.name : o.tag;
    std::string c = !o.tag.empty() ? o.tag : o.name;
    const std::string& r = o.tag.size() > 9 ? o.tag : o.name;
    std::printf("%s %s %s %s\n", a.c_str(), b.c_str(), c.c_str(), r.c_str());
    return 0;
}
