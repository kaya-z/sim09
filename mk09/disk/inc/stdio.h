/* stdio.h - for the C compiler of MK09 (cc).  stdout is written at a newline (or when 80 characters wait); stderr is not buffered. */
#ifndef _STDIO_H
#define _STDIO_H
#define EOF (-1)
struct _FILE;
typedef struct _FILE FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;
int printf(char *fmt, ...);             /* %d %u %x %c %s %%, a width, - and 0 */
int fprintf(FILE *f, char *fmt, ...);
int sprintf(char *buf, char *fmt, ...);
int putchar(int c);
int puts(char *s);                      /* adds a newline */
int getchar(void);                      /* -1 at the end of the input */
int readline(char *buf, int n);         /* a line of the standard input without the newline; -1 at the end of the input */
FILE *fopen(char *path, char *mode);    /* "r" "w" "a" "r+" "w+" */
int fclose(FILE *f);
int fflush(FILE *f);                    /* fflush(stdout) or fflush(NULL): the console output */
int fputc(int c, FILE *f);
int fgetc(FILE *f);
int putc(int c, FILE *f);
int getc(FILE *f);
int ungetc(int c, FILE *f);
int fputs(char *s, FILE *f);            /* no newline */
char *fgets(char *buf, int n, FILE *f);
unsigned fread(char *p, unsigned size, unsigned n, FILE *f);
unsigned fwrite(char *p, unsigned size, unsigned n, FILE *f);
int fseek(FILE *f, int off, int whence);    /* offsets of 16 bits */
int ftell(FILE *f);
int feof(FILE *f);
int ferror(FILE *f);
int remove(char *path);
#endif
