// Our own streams (<sstream>, <fstream>, <iostream>, <iomanip>) and std::to_string / std::stoi.
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

struct V2 {
    float x, y;
};
std::ostream &operator<<(std::ostream &os, const V2 &v) { return os << "(" << v.x << ", " << v.y << ")"; }

static std::string readAll(const char *path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int main() {
    std::ostringstream o;
    o << "int " << 42 << " neg " << -7 << " ul " << 18446744073709551615UL << " d " << 3.25 << " f " << 0.1f
      << " big " << 1e20 << " small " << 1.5e-7 << " c " << 'z' << " b " << true << " v " << V2{1.5f, -2};
    std::printf("%s\n", o.str().c_str());
    std::ostringstream h;
    h << std::hex << 255 << " " << std::dec << 255 << " " << std::setw(6) << 42 << "|" << std::left << std::setw(5)
      << "ab" << "|" << std::setfill('0') << std::right << std::setw(4) << 7 << " " << std::fixed
      << std::setprecision(2) << 3.14159 << " " << std::boolalpha << false;
    std::printf("%s\n", h.str().c_str());

    std::istringstream in("12 -5 3.5 word 0x1f\nsecond line\nthird");
    int a = 0, b = 0;
    double d = 0;
    std::string w;
    in >> a >> b >> d >> w;
    std::printf("%d %d %g %s %d\n", a, b, d, w.c_str(), (bool)in);
    std::string rest, line;
    std::getline(in, rest);
    std::getline(in, line);
    std::printf("[%s] [%s]\n", rest.c_str(), line.c_str());
    std::getline(in, line);
    std::printf("[%s] eof=%d fail=%d\n", line.c_str(), in.eof(), in.fail());
    std::getline(in, line);
    std::printf("after end fail=%d\n", in.fail());

    std::istringstream bad("abc");
    int n = 99;
    bad >> n;
    std::printf("bad %d %d\n", n, bad.fail());

    std::stringstream ss;
    ss << 10 << ' ' << 20;
    int p = 0, q = 0;
    ss >> p >> q;
    std::printf("ss %d %d\n", p, q);
    std::vector<std::string> words;
    std::istringstream ws("a bb  ccc\tdddd");
    for (std::string t; ws >> t;) words.push_back(t);
    std::printf("words %d %s\n", (int)words.size(), words.back().c_str());

    const char *path = "/tmp/okaycc_stream22.txt";
    {
        std::ofstream out(path);
        out << "hello " << 5 << "\n" << 2.5 << "\n";
        out.write("raw", 3);
        std::printf("open %d\n", out.is_open());
    }
    std::string all = readAll(path);
    std::printf("[%s] %d\n", all.c_str(), (int)all.size());
    {
        std::ofstream app(path, std::ios::app);
        app << "+more";
    }
    std::ifstream f(path);
    std::string first;
    int num = 0;
    f >> first >> num;
    f.seekg(0, std::ios::end);
    long size = (long)f.tellg();
    f.seekg(0);
    char buf[5] = {0};
    f.read(buf, 4);
    std::printf("%s %d %ld %s %ld\n", first.c_str(), num, size, buf, (long)f.gcount());
    std::ifstream it(path);
    std::string viaIter((std::istreambuf_iterator<char>(it)), std::istreambuf_iterator<char>());
    std::printf("iter %d\n", (int)viaIter.size());
    std::ifstream missing("/tmp/okaycc_no_such_file_22");
    std::printf("missing %d %d\n", missing.is_open(), (bool)missing);
    std::remove(path);

    std::printf("%s %s %s\n", std::to_string(-12).c_str(), std::to_string(7UL).c_str(), std::to_string(1.5).c_str());
    std::printf("%d %ld %g\n", std::stoi("  42x"), std::stol("-9"), std::stod("2.75"));
    std::cout << "cout " << 1 << std::endl;
    std::cout.flush();
    std::cerr << "cerr\n";
    return 0;
}
