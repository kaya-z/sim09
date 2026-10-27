/* m_file.c - the files of MK09 through mklib.h (run only on MK09; the output is in m_file.out) */
#include <stdio.h>
#include <string.h>
#include <mklib.h>

int main(int argc, char **argv)
{
    char buf[32];
    int fd, n;
    fd = open("/mfile.txt", FO_WRITE | FO_CREATE | FO_TRUNC);
    printf("open %d\n", fd >= 3);
    n = write(fd, "hello, file\n", 12);
    printf("wrote %d\n", n);
    close(fd);
    fd = open("/mfile.txt", FO_READ);
    n = read(fd, buf, 5);
    buf[n] = 0;
    printf("read %d [%s]\n", n, buf);
    printf("seek %d\n", seek(fd, 7, 0));
    n = read(fd, buf, 31);
    buf[n] = 0;
    printf("read %d [%s]\n", n, buf);
    printf("eof %d\n", read(fd, buf, 4));
    close(fd);
    printf("noent %d\n", open("/nosuch.txt", FO_READ) == -E_NOENT);
    printf("unlink %d\n", unlink("/mfile.txt"));
    printf("again %d\n", unlink("/mfile.txt") == -E_NOENT);
    printf("argc %d\n", argc);
    return 3;
}
