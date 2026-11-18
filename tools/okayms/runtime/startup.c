__declspec(dllimport) void ExitProcess(unsigned int code);
int main(void);

void mainCRTStartup(void) {
    int code;
    code = main();
    ExitProcess(code);
}
