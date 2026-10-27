/* mklib.h - the files of MK09 for the C compiler (cc).  A file handle (fd) is 0 = the standard input, 1 = the standard output, 3 and up = files;
 * a function returns a value >= 0 or the negated error code (E_*). */
#ifndef _MKLIB_H
#define _MKLIB_H
#define FO_READ   1
#define FO_WRITE  2
#define FO_CREATE 4
#define FO_TRUNC  8
#define FO_APPEND 16
#define E_NOENT   9
#define E_EXIST   10
#define E_NOSPC   13
#define E_BADF    15
int open(char *path, int flags);
int close(int fd);
int read(int fd, char *buf, unsigned n);        /* 0 at the end of a file */
int write(int fd, char *buf, unsigned n);
int seek(int fd, int off, int whence);          /* 0 start, 1 here, 2 end; returns the position (the low 16 bits) */
int unlink(char *path);
int mkdir(char *path);
int chdir(char *path);
int getcwd(char *buf, unsigned n);
#endif
