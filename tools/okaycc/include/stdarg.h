typedef struct {
    unsigned gp_offset;
    unsigned fp_offset;
    char *overflow_arg_area;
    char *reg_save_area;
} __va_elem;
typedef __va_elem va_list[1];
void __va_start(va_list ap);
char *__va_argp(va_list ap, void *typed);
#define va_start(ap, last) __va_start(ap)
#define va_arg(ap, type) (*(type *)__va_argp(ap, (type *)0))
#define va_end(ap)
#define va_copy(dst, src) ((dst)[0] = (src)[0])
