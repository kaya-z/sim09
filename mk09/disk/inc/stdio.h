/* stdio.h - for the C compiler of MK09 (cc): the console and the standard files only; no FILE */
#ifndef _STDIO_H
#define _STDIO_H
#define EOF (-1)
int printf(char *fmt, ...);             /* %d %u %x %c %s %%, a width, - and 0 */
int sprintf(char *buf, char *fmt, ...);
int putchar(int c);
int puts(char *s);                      /* adds a newline */
int getchar(void);                      /* -1 at the end of the input */
int readline(char *buf, int n);         /* a line without the newline; -1 at the end of the input */
int fflush(void);                       /* the output of printf, putchar and puts is kept until a newline */
#endif
