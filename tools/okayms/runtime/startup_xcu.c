/* Startup that runs the global constructors of every file: each file compiled with /okay:xcu leaves a pointer to its __okay_init in
   a .CRT$XCU section; the linker gathers them in .CRT and names its ends __okay_xcu_begin and __okay_xcu_end. */
__declspec(dllimport) void ExitProcess(unsigned int code);
int main(void);
extern char __okay_xcu_begin;
extern char __okay_xcu_end;

void mainCRTStartup(void) {
    void (**fn)(void);
    int code;
    for (fn = (void (**)(void))&__okay_xcu_begin; (char *)fn < &__okay_xcu_end; ++fn)
        if (*fn != (void (*)(void))0) (*fn)();
    code = main();
    ExitProcess(code);
}
