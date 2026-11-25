#pragma once

#include <string>
#include <vector>

namespace okaycc {

struct Diag {
    std::string file;
    int line = 1;
    int column = 1;
    std::string message;
    bool warning = false;
};

// Collected front-end errors. The compiler does not throw for a bad program.
class Diagnostics {
public:
    void error(const std::string& file, int line, int column, const std::string& message);
    void warning(const std::string& file, int line, int column, const std::string& message);
    bool ok() const {
        for (const Diag& d : items_)
            if (!d.warning) return false;
        return true;
    }
    const std::vector<Diag>& items() const { return items_; }
    std::string format() const;
    void clear() { items_.clear(); }
    // Drops the items after the first n (a tentative read that is taken back).
    void truncate(std::size_t n) {
        if (items_.size() > n) items_.resize(n);
    }

private:
    std::vector<Diag> items_;
};

}  // namespace okaycc
