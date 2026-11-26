// string::append(str, pos, n) ([string.append] p3), string::insert(pos, s, n) and insert(pos, n, c)
// ([string.insert]), string_view::compare(pos, n, ..)
// ([string.view.ops]) and an operator on a member of an element chosen by a call
// (x.n[(size_t)stack.back()].name != nm), as in Render/XmlLite.hpp.
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>
struct Node {
    std::string name;
    std::string text;
};
struct Doc {
    std::vector<Node> n;
};
int main() {
    std::string s = "<a>hello &amp; world</a>";
    std::string dst;
    dst.append(s, 3, 5);
    dst.append(s, 8, 100);
    std::printf("[%s]\n", dst.c_str());
    std::string self = "abc";
    self.append(self, 1, 2);
    std::printf("[%s]\n", self.c_str());
    std::string_view an("xmlns:foo");
    std::printf("%d %d %d\n", an.compare(0, 6, "xmlns:") == 0, an.compare(0, 5, std::string_view("xmlnt")) < 0,
                an.compare(6, 3, "foo", 3) == 0);
    std::printf("%d\n", an.compare(0, 2, std::string_view("--xm--"), 2, 2) == 0);
    Doc x;
    x.n.push_back(Node{"root", ""});
    x.n.push_back(Node{"item", ""});
    std::vector<int> stack = {0, 1};
    const std::string nm = "item";
    if (x.n[(std::size_t)stack.back()].name != nm) std::printf("mismatch\n");
    else std::printf("match\n");
    x.n[(std::size_t)stack.back()].text += "t";
    x.n[(std::size_t)stack.front()].text.append(s, 3, 5);
    std::printf("%s %s %d\n", x.n[1].text.c_str(), x.n[0].text.c_str(), x.n[(std::size_t)stack.front()].name == "root");
    std::string t = "held";
    t.insert(2, 1, '\n');
    t.insert(0, "abcdef" + 1, 3);
    t.insert(t.size(), 2, '!');
    t.insert(1, t.c_str() + 2, 4);
    for (char c : t) std::printf("%c", c == '\n' ? '_' : c);
    std::printf("\n");
    return 0;
}
