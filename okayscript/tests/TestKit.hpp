#pragma once
// A tiny test harness for the OkayScript tests (standard library only, like the language itself).
#include "okayscript/OkayScript.hpp"
#include <iostream>
#include <sstream>
#include <string>

namespace oktest {
inline int g_failures = 0;
inline int g_checks = 0;
inline const char* g_test = "";

template <class A, class B>
void CheckEq(const A& a, const B& b, const char* ea, const char* eb, const char* file, int line) {
    ++g_checks;
    if (a == b) return;
    ++g_failures;
    std::ostringstream sa, sb;
    sa << a; sb << b;
    std::cerr << "  FAIL [" << g_test << "] " << ea << " == " << eb << "\n    got:      " << sa.str()
              << "\n    expected: " << sb.str() << "\n    (" << file << ":" << line << ")\n";
}

/// Run `src` with the standard library; return what it printed. A script error is returned as "ERROR <kind>
/// <line>: <message>" (kind = syntax or runtime) after whatever was printed before it.
inline std::string Run(const std::string& src, okayscript::Interpreter* use = nullptr) {
    okayscript::Interpreter local;
    okayscript::Interpreter& in = use ? *use : local;
    if (!use) okayscript::InstallStandardLibrary(in);
    std::ostringstream out;
    in.SetOutput(&out);
    try {
        in.Run(src, "test.okay");
    } catch (const okayscript::SyntaxError& e) {
        out << "ERROR syntax " << e.line << ": " << e.what();
    } catch (const okayscript::RuntimeError& e) {
        out << "ERROR runtime " << e.line << ": " << e.what();
    }
    in.SetOutput(nullptr);
    return out.str();
}

/// The value of a one-expression program (`return <expr>` is not needed: the expression's value is printed).
inline std::string Eval(const std::string& expr) {
    std::string s = Run("print(" + expr + ")");
    if (!s.empty() && s.back() == '\n') s.pop_back();
    return s;
}
} // namespace oktest

#define TEST(name) oktest::g_test = name;
#define CHECK(cond) oktest::CheckEq((bool)(cond), true, #cond, "true", __FILE__, __LINE__)
#define CHECK_EQ(a, b) oktest::CheckEq((a), (b), #a, #b, __FILE__, __LINE__)
#define TEST_RESULT()                                                                                          \
    do {                                                                                                       \
        std::cout << oktest::g_checks << " checks, " << oktest::g_failures << " failures\n";                   \
        return oktest::g_failures == 0 ? 0 : 1;                                                                \
    } while (0)
