/* string.h - for the C compiler of MK09 (cc) */
#ifndef _STRING_H
#define _STRING_H
unsigned strlen(char *s);
char *strcpy(char *d, char *s);
char *strncpy(char *d, char *s, unsigned n);
char *strcat(char *d, char *s);
int strcmp(char *a, char *b);
int strncmp(char *a, char *b, unsigned n);
char *strchr(char *s, int c);
char *strrchr(char *s, int c);
char *strstr(char *h, char *n);
char *memcpy(char *d, char *s, unsigned n);
char *memmove(char *d, char *s, unsigned n);
char *memset(char *d, int c, unsigned n);
int memcmp(char *a, char *b, unsigned n);
#endif
