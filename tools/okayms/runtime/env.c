/* getenv for okay-ms programs, written from ISO/IEC 9899 7.22.4.6 on the Windows call GetEnvironmentVariableA. */
__declspec(dllimport) unsigned long GetEnvironmentVariableA(const char *name, char *buffer, unsigned long size);

static char env_buffer[4096];

char *getenv(const char *name) {
    unsigned long n = GetEnvironmentVariableA(name, env_buffer, 4096);
    if (n == 0 || n >= 4096) return 0;
    return env_buffer;
}
