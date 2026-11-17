#pragma once

#include <string>
#include <vector>

namespace okaycc {

struct Diag {
    std::string file;
    int line = 1;
    int column = 1;
    std::string message;
};

// Collected front-end errors. The compiler does not throw for a bad program.
class Diagnostics {
public:
    void error(const std::string& file, int line, int column, const std::string& message);
    bool ok() const { return items_.empty(); }
    const std::vector<Diag>& items() const { return items_; }
    std::string format() const;
    void clear() { items_.clear(); }

private:
    std::vector<Diag> items_;
};

}  // namespace okaycc
