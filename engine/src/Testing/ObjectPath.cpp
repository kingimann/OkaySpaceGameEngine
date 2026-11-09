#include "okay/Testing/ObjectPath.hpp"

#include <cstdlib>
#include <typeinfo>
#if defined(__GNUC__) || defined(__clang__)
#  include <cxxabi.h>
#endif

namespace okay::testing {

std::string ComponentTypeName(const Component& component) {
    std::string name = typeid(component).name();
#if defined(__GNUC__) || defined(__clang__)
    int status = 0;
    char* demangled = abi::__cxa_demangle(name.c_str(), nullptr, nullptr, &status);
    if (demangled) {
        if (status == 0) name = demangled;
        std::free(demangled);
    }
#endif
    // MSVC prefixes "class " / "struct "; the namespace is dropped everywhere.
    for (const char* prefix : {"class ", "struct "})
        if (name.compare(0, std::char_traits<char>::length(prefix), prefix) == 0)
            name.erase(0, std::char_traits<char>::length(prefix));
    if (name.compare(0, 6, "okay::") == 0) name.erase(0, 6);
    return name;
}

} // namespace okay::testing
