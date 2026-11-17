/* Declarations for the small okay-cl runtime. */

void *memcpy(void *dst, void *src, unsigned int n);
void *memset(void *dst, int value, unsigned int n);
unsigned int strlen(char *s);
int strcmp(char *a, char *b);
int printf(char *fmt, ...);
int main(void);
