/* stdarg.h - for the C compiler of MK09 (cc): every argument takes 2 bytes on the stack (a char argument is an int: use va_arg(ap, int)) */
#ifndef _STDARG_H
#define _STDARG_H
typedef char *va_list;
#define va_start(ap, last) ((ap) = (char *)&(last) + 2)
#define va_arg(ap, type) (*(type *)(((ap) += 2) - 2))
#define va_end(ap) ((void)0)
#endif
