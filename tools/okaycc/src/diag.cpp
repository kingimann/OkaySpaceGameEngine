#include "diag.hpp"

#include <string>

namespace okaycc {

void Diagnostics::error(const std::string& file, int line, int column, const std::string& message) {
    items_.push_back(Diag{file, line, column, message});
}

std::string Diagnostics::format() const {
    std::string out;
    for (const Diag& d : items_) {
        out += d.file;
        out += ":";
        out += std::to_string(d.line);
        out += ":";
        out += std::to_string(d.column);
        out += ": error: ";
        out += d.message;
        out += "\n";
    }
    return out;
}

}  // namespace okaycc
