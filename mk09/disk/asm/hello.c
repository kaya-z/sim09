/* hello.c - a C program compiled on MK09:   cc /asm/hello.c -o /hello_c.mx   then   /hello_c.mx a b */
int printf(char *fmt, ...);

int main(int argc, char **argv)
{
    int i;
    printf("hello from C, %d argument(s)\n", argc);
    for (i = 0; i < argc; i++) printf("  %d: %s\n", i, argv[i]);
    return 0;
}
