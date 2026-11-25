// get<0>(tuple) and get<T>(variant) side by side: two function templates of one name whose first
// parameters differ in kind (a value, a type) ([temp.arg.explicit], [over.match]).
#include <variant>
#include <tuple>
#include <cstdio>
int main() {
    std::tuple<int, long long> t(4, 5);
    std::variant<int, double> v = 2.5;
    std::printf("%d %lld %g\n", std::get<0>(t), std::get<1>(t), std::get<double>(v));
    std::get<0>(t) = 9;
    std::get<double>(v) = 1.5;
    std::printf("%d %g\n", std::get<0>(t), std::get<double>(v));
}
