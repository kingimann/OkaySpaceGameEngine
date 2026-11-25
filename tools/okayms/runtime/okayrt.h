/* Declarations for the small okay-cl runtime. */

void *memcpy(void *dst, void *src, unsigned int n);
void *memset(void *dst, int value, unsigned int n);
void *memchr(void *s, int c, unsigned long long n);
void *malloc(unsigned int n);
void free(void *p);
unsigned int strlen(char *s);
int strcmp(char *a, char *b);
char *strcpy(char *dst, char *src);
char *strcat(char *dst, char *src);
int atoi(char *s);
int printf(char *fmt, ...);
int snprintf(char *buf, unsigned int cap, char *fmt, ...);
int puts(char *s);
int main(void);
