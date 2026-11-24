// okayscript: run an OkayScript program from the command line. Links only okayscript_lang (never the engine).
//
//   okayscript file.okay [args...]    run the file's top level, then main() if it defines one
//   okayscript -e "code" [args...]    run code given on the command line
//   okayscript --version | --help
//
// The extra arguments are the global list `args` (and main's first parameter, when it has one).
// Exit codes: 0 success, 1 runtime error, 2 syntax error, 3 usage or unreadable file; exit(n) in a script ends with n.
// Errors go to stderr as "file:line: message".
#include "okayscript/OkayScript.hpp"
#include <fstream>
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
         "Exit codes: 0 ok, 1 runtime error, 2 syntax error, 3 usage or unreadable file.\n";
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
    }
    std::cout.flush();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> argsIn(argv + 1, argv + argc);
    if (argsIn.empty()) { Usage(std::cerr); return 3; }
    const std::string first = argsIn[0];
    if (first == "--help" || first == "-h") { Usage(std::cout); return 0; }
    if (first == "--version") { std::cout << "okayscript " << kVersion << "\n"; return 0; }
    if (first == "-e") {
        if (argsIn.size() < 2) { std::cerr << "okayscript: -e needs the code to run\n"; Usage(std::cerr); return 3; }
        return RunProgram(argsIn[1], "-e", std::vector<std::string>(argsIn.begin() + 2, argsIn.end()));
    }
    if (!first.empty() && first[0] == '-') { std::cerr << "okayscript: unknown option " << first << "\n"; Usage(std::cerr); return 3; }
    std::ifstream f(first, std::ios::binary);
    if (!f) { std::cerr << "okayscript: can't read '" << first << "'\n"; return 3; }
    std::ostringstream ss;
    ss << f.rdbuf();
    return RunProgram(ss.str(), first, std::vector<std::string>(argsIn.begin() + 1, argsIn.end()));
}
