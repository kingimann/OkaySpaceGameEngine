// Round 21: our own <cstdio> (lib/printf.c formatting and snprintf, lib/stdio.c files) against glibc.
#include <cstdio>
int main() {
    char b[64];
    int n = std::snprintf(b, sizeof b, "[%5d|%-5d|%05d|%+d|% d|%x|%X|%#x|%o|%#o|%.3d]", 42, 42, 42, 42, 42, 255, 255, 255, 8, 8, 7);
    std::printf("%s %d\n", b, n);
    n = std::snprintf(b, 8, "%s-%s", "abcdef", "ghijk");
    std::printf("%s %d\n", b, n);
    std::printf("[%c|%3c|%-3c|%.2s|%*d|%-*d|%.*f]\n", 'a', 'b', 'c', "xyz", 4, 7, 4, 7, 2, 3.14159);
    std::printf("%ld %lld %lu %zu %hd %hhd %hu %i\n", -5L, -6LL, 7UL, (unsigned long)8, (short)-9, (signed char)-10, (unsigned short)65535, -11);
    std::printf("[%p] [%10s] [%-10s] [%%]\n", (void *)0, "right", "left");
    std::printf("%e %g %G %10.3f|%-10.2e|% f\n", 12345.678, 0.0001234, 1e20, 3.14159, 2.5, 1.5);
    std::fprintf(stdout, "to stdout %d\n", 1);
    const char *path = "/tmp/okaycc_cstdio21.txt";
    FILE *f = std::fopen(path, "w");
    std::fprintf(f, "line one %d\nline two\n", 1);
    std::fputs("third\n", f);
    std::fputc('Z', f);
    unsigned long w = std::fwrite("0123456789", 1, 10, f);
    std::fclose(f);
    f = std::fopen(path, "r");
    char line[32];
    while (std::fgets(line, sizeof line, f)) std::printf("got: %s", line);
    std::printf("\neof %d\n", std::feof(f) ? 1 : 0);
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 5, SEEK_SET);
    char part[4] = {0, 0, 0, 0};
    unsigned long r = std::fread(part, 1, 3, f);
    std::printf("size %ld wrote %lu read %lu [%s] at %ld\n", size, w, r, part, std::ftell(f));
    std::fseek(f, -2, SEEK_CUR);
    std::printf("c=%c\n", std::fgetc(f));
    std::fclose(f);
    int rm1 = std::remove(path);
    int rm2 = std::remove(path);
    std::printf("remove %d %d\n", rm1, rm2);
    std::FILE *g = std::fopen("/nonexistent/dir/x", "r");
    std::printf("null %d\n", g == nullptr ? 1 : 0);
    return 0;
}
