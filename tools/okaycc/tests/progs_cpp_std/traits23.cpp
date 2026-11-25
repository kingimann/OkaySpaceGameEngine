// Round 23: <type_traits> (our own), static_assert, if constexpr, variable templates (X_v<T>), alias
// templates (X_t<T>), value template parameters of a dependent type (template <class T, T v>), and
// value template arguments that are trait expressions.
#include <cstdio>
#include <string>
#include <type_traits>
struct Component {
    virtual ~Component() {}
    virtual void Tick() = 0;
};
struct Light : Component {
    void Tick() override {}
};
struct Spot final : Light {};
struct Plain {
    int x;
};
struct Empty {};
struct Owner {
    std::string s;
};
struct NoDefault {
    explicit NoDefault(int v) : x(v) {}
    int x;
};
static_assert(std::is_same<int, int>::value, "same");
static_assert(!std::is_same_v<int, long>, "not same");
static_assert(std::is_base_of_v<Component, Spot>, "base");

template <class T>
static const char *kind() {
    if constexpr (std::is_integral_v<T>) return "integral";
    else if constexpr (std::is_floating_point<T>::value) return "floating";
    else if constexpr (std::is_pointer_v<T>) return "pointer";
    else if constexpr (std::is_base_of<Component, T>::value) return "component";
    else return "other";
}
template <class T>
static int size_or_zero(const T &v) {
    if constexpr (std::is_same_v<T, std::string>) return (int)v.size();
    else return 0;
}
template <class T, T v>
struct Const {
    static constexpr T value = v;
};
template <bool B>
static int pick() { return std::conditional_t<B, Const<int, 7>, Const<int, 9>>::value; }

int main() {
    std::printf("%s %s %s %s %s %s\n", kind<int>(), kind<double>(), kind<const char *>(), kind<Light>(),
                kind<Plain>(), kind<bool>());
    std::printf("size %d %d\n", size_or_zero(std::string("hello")), size_or_zero(3));
    std::printf("base %d %d %d %d\n", (int)std::is_base_of<Component, Light>::value,
                (int)std::is_base_of<Light, Component>::value, (int)std::is_base_of_v<Light, Light>,
                (int)std::is_base_of_v<Plain, Light>);
    std::printf("props poly=%d abs=%d,%d final=%d,%d empty=%d,%d\n", (int)std::is_polymorphic_v<Light>,
                (int)std::is_abstract_v<Component>, (int)std::is_abstract_v<Light>, (int)std::is_final_v<Spot>,
                (int)std::is_final_v<Light>, (int)std::is_empty_v<Empty>, (int)std::is_empty_v<Plain>);
    std::printf("life triv=%d,%d copy=%d,%d def=%d,%d,%d\n", (int)std::is_trivially_destructible_v<Plain>,
                (int)std::is_trivially_destructible_v<Owner>, (int)std::is_trivially_copyable_v<Plain>,
                (int)std::is_trivially_copyable_v<Owner>, (int)std::is_default_constructible_v<Plain>,
                (int)std::is_default_constructible_v<NoDefault>, (int)std::is_default_constructible_v<Component>);
    std::remove_pointer_t<Light *> l;
    std::remove_reference_t<int &> n = 5;
    std::remove_const_t<const int> m = 6;
    m += n;
    std::decay_t<const int &> d = m;
    std::printf("trans %d %d %d\n", n, m, d + (int)sizeof(l));
    std::printf("cond %d %d const %d\n", pick<true>(), pick<false>(), Const<long, -3>::value);
    std::printf("sign %d %d %d\n", (int)std::is_signed_v<int>, (int)std::is_signed_v<unsigned>,
                (int)std::is_unsigned_v<unsigned char>);
    std::printf("tf %d %d %d\n", (int)std::true_type::value, (int)std::false_type::value,
                (int)std::integral_constant<int, 4>::value);
    return 0;
}
