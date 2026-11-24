// okayscript: run an OkayScript program from the command line. Links only okayscript_lang (never the engine).
//
//   okayscript file.okay [args...]    run the file's top level, then main() if it defines one
//   okayscript -- file.okay [args...] the same, for a file name that starts with '-'
//   okayscript -e "code" [args...]    run code given on the command line
//   okayscript --version | --help
//
// The extra arguments are the global list `args` (and main's first parameter, when it has one).
// Exit codes: 0 success, 1 runtime error (also out of memory), 2 syntax error, 64 usage error, 66 unreadable file;
// exit(n) in a script ends with n (0..255). Errors go to stderr as "file:line: message".
#include "okayscript/OkayScript.hpp"
#include <exception>
#include <fstream>
#include <new>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr const char* kVersion = "0.1.0";

void Usage(std::ostream& o) {
    o << "usage: okayscript <file.okay> [args...]\n"
         "       okayscript -e \"code\" [args...]\n"
         "       okayscript --version\n"
         "Runs the program's top level, then main() if it is defined.\n"
         "Exit codes: 0 ok, 1 runtime error, 2 syntax error, 64 usage error, 66 unreadable file;\n"
         "exit(n) in a script ends with n.\n";
}

int RunProgram(const std::string& source, const std::string& file, const std::vector<std::string>& extra) {
    okayscript::Interpreter in;
    okayscript::InstallStandardLibrary(in);
    in.GetLimits().maxLoopIterations = 0;   // a program may loop as long as it likes; recursion stays capped
    okayscript::Value args = okayscript::Value::MakeArray();
    for (const std::string& a : extra) args.AsArray()->push_back(okayscript::Value{a});
    in.SetGlobal("args", args);
    try {
        in.Run(source, file);
        if (in.HasFunction("main")) {
            std::vector<okayscript::Value> mainArgs;
            if (!in.FunctionParams("main").empty()) mainArgs.push_back(args);
            in.Call("main", mainArgs);
        }
    } catch (const okayscript::ExitRequest& e) {
        std::cout.flush();
        return e.code;
    } catch (const okayscript::SyntaxError& e) {
        std::cout.flush();
        std::cerr << okayscript::FormatError(file, e) << "\n";
        return 2;
    } catch (const okayscript::ScriptError& e) {
        std::cout.flush();
        std::cerr << okayscript::FormatError(file, e) << "\n";
        return 1;
    } catch (const std::bad_alloc&) {
        std::cout.flush();
        std::cerr << file << ": out of memory\n";
        return 1;
    } catch (const std::exception& e) {
        std::cout.flush();
        std::cerr << file << ": " << e.what() << "\n";
        return 1;
    }
    std::cout.flush();
    return 0;
}

} // namespace

constexpr int kUsage = 64;     // EX_USAGE: the command line was wrong
constexpr int kNoInput = 66;   // EX_NOINPUT: the file could not be read

int main(int argc, char** argv) {
    std::vector<std::string> argsIn(argv + 1, argv + argc);
    if (argsIn.empty()) { Usage(std::cerr); return kUsage; }
    std::size_t at = 0;
    const std::string first = argsIn[0];
    if (first == "--") {
        at = 1;   // everything after `--` is the file and its arguments, even if it starts with '-'
        if (argsIn.size() < 2) { std::cerr << "okayscript: -- needs a file name\n"; Usage(std::cerr); return kUsage; }
    } else {
        if (first == "--help" || first == "-h") { Usage(std::cout); return 0; }
        if (first == "--version") { std::cout << "okayscript " << kVersion << "\n"; return 0; }
        if (first == "-e") {
            if (argsIn.size() < 2) { std::cerr << "okayscript: -e needs the code to run\n"; Usage(std::cerr); return kUsage; }
            return RunProgram(argsIn[1], "-e", std::vector<std::string>(argsIn.begin() + 2, argsIn.end()));
        }
        if (!first.empty() && first[0] == '-') {
            std::cerr << "okayscript: unknown option " << first << "\n";
            Usage(std::cerr);
            return kUsage;
        }
    }
    const std::string& path = argsIn[at];
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::cerr << "okayscript: can't read '" << path << "'\n"; return kNoInput; }
    std::ostringstream ss;
    ss << f.rdbuf();
    return RunProgram(ss.str(), path, std::vector<std::string>(argsIn.begin() + (std::ptrdiff_t)at + 1, argsIn.end()));
}
