__declspec(dllimport) void ExitProcess(unsigned int code);
int main(void);

/* The start-up tables (Round 35; ISO/IEC 14882 [basic.start.dynamic], [basic.start.term], [basic.stc.thread]; PE/COFF
   "Grouped Sections" and "TLS Callback Functions"). okay-ms puts the address of each object's start-up function (its
   globals' constructors) in .CRT$XCU and of its end function (their destructors) in .CRT$XTU, and those of its
   thread_local objects in .CRT$XDU and .okaytd$U; the compiler gives these markers their sections by name (.CRT$XCA,
   .CRT$XCZ, ...), and the linker puts every object's entries between a table's two markers. The object with main runs
   its own start-up and end functions from main. Our own design; the walkers skip null entries. */
typedef void (*okay_pfv)(void);
static okay_pfv __okay_xc_a = 0;
static okay_pfv __okay_xc_z = 0;
static okay_pfv __okay_xt_a = 0;
static okay_pfv __okay_xt_z = 0;
static okay_pfv __okay_xd_a = 0;
static okay_pfv __okay_xd_z = 0;
static okay_pfv __okay_td_a = 0;
static okay_pfv __okay_td_z = 0;

static void run_forward(okay_pfv *from, okay_pfv *to) {
    okay_pfv *p;
    p = from + 1;
    while (p < to) {
        if (*p) (*p)();
        p = p + 1;
    }
}

static void run_backward(okay_pfv *from, okay_pfv *to) {
    okay_pfv *p;
    p = to;
    while (p > from + 1) {
        p = p - 1;
        if (*p) (*p)();
    }
}

/* The image's TLS callback when the object with main has none of its own (the linker prefers __okay_tls_callback):
   the thread_local objects of a new thread are made (reason 1: main's thread, 2: any other), and destroyed when it ends
   (3). */
void __okay_tls_callback_rt(void *module, unsigned int reason, void *reserved) {
    if (reason == 1 || reason == 2) run_forward(&__okay_xd_a, &__okay_xd_z);
    else if (reason == 3) run_backward(&__okay_td_a, &__okay_td_z);
}

void mainCRTStartup(void) {
    int code;
    run_forward(&__okay_xc_a, &__okay_xc_z);
    code = main();
    run_backward(&__okay_xt_a, &__okay_xt_z);
    ExitProcess(code);
}
