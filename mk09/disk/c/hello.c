/* hello.c - the shortest program: the arguments.   cc /c/hello.c -o /hello.mx   then   /hello.mx a b */
#include <stdio.h>

int main(int argc, char **argv)
{
    int i;
    printf("hello from C, %d argument(s)\n", argc);
    for (i = 0; i < argc; i++) printf("  %d: %s\n", i, argv[i]);
    return 0;
}
