// Our own <exception>, <stdexcept> and <system_error>: what(), catching by a base class, error_code.
#include <cstdio>
#include <stdexcept>
#include <string>
#include <system_error>

static int parse(const std::string &s) {
    if (s.empty()) throw std::invalid_argument("empty string");
    if (s.size() > 3) throw std::out_of_range("too long: " + s);
    return (int)s.size();
}

int main() {
    const char *inputs[] = {"ab", "", "abcdef"};
    for (const char *in : inputs) {
        try {
            std::printf("ok %d\n", parse(in));
        } catch (const std::invalid_argument &e) {
            std::printf("invalid: %s\n", e.what());
        } catch (const std::exception &e) {
            std::printf("other: %s\n", e.what());
        }
    }
    try {
        throw std::runtime_error("boom");
    } catch (const std::exception &e) {
        std::printf("%s\n", e.what());
    }
    std::error_code ec;
    std::printf("%d %d [%s]\n", (bool)ec, ec.value(), ec.message().c_str());
    ec = std::error_code(2, std::system_category());
    std::printf("%d %d [%s] %s\n", (bool)ec, ec.value(), ec.message().c_str(), ec.category().name());
    std::error_code e2 = std::make_error_code(std::errc::file_exists);
    std::printf("%d [%s] %d\n", e2.value(), e2.message().c_str(), e2 == std::errc::file_exists);
    try {
        throw std::system_error(std::error_code(13, std::generic_category()), "open");
    } catch (const std::system_error &e) {
        std::printf("%s %d\n", e.what(), e.code().value());
    }
    return 0;
}
