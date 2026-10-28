/* stdlib.h - for the C compiler of MK09 (cc) */
#ifndef _STDLIB_H
#define _STDLIB_H
void exit(int code);
int abs(int v);
int atoi(char *s);
long atol(char *s);
float atof(char *s);
float strtod(char *s, char **end);
long labs(long v);
int rand(void);                         /* 0..32767 */
void srand(unsigned seed);
char *malloc(unsigned n);               /* from a heap of 3000 bytes */
void free(char *p);
char *calloc(unsigned n, unsigned size);
void qsort(char *base, unsigned n, unsigned size, int (*cmp)(char *, char *));
#endif
