// main's argc, argv and envp from our start code (System V AMD64 ABI 3.4.1).
#include <cstdio>
#include <cstring>
#include <string>
struct Banner {
    int n = 0;
    Banner() { n = 7; }
};
Banner g;
int main(int argc, char** argv, char** envp) {
    int envs = 0;
    bool home = false;
    for (char** e = envp; *e; ++e) {
        ++envs;
        if (std::strncmp(*e, "PATH=", 5) == 0) home = true;
    }
    std::string first = argv[0];
    std::printf("argc %d, argv[argc] %s, argv0 set %d, env %d, PATH %d, g %d\n", argc, argv[argc] ? "set" : "null",
                first.size() > 0 ? 1 : 0, envs > 0 ? 1 : 0, home ? 1 : 0, g.n);
    return argc + 2;
}
