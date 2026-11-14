/* strings.c - the string functions: reverse a word, count the vowels, find a word, upper case, sprintf */
#include <stdio.h>
#include <string.h>
#include <ctype.h>

void reverse(char *s)
{
    int i = 0, j = strlen(s) - 1;
    char t;
    while (i < j) { t = s[i]; s[i] = s[j]; s[j] = t; i++; j--; }
}

int main(void)
{
    char buf[40], out[60];
    char *text = "the quick brown fox jumps over the lazy dog";
    char *p;
    int vowels = 0;

    strcpy(buf, "MK09");
    strcat(buf, " on the FM-11");
    printf("%s (%d characters)\n", buf, strlen(buf));
    reverse(buf);
    printf("reversed: %s\n", buf);
    for (p = text; *p; p++)
        if (strchr("aeiou", *p)) vowels++;
    printf("%d vowels in: %s\n", vowels, text);
    p = strstr(text, "fox");
    printf("\"fox\" is at %d, the rest: %s\n", p - text, p);
    for (p = buf; *p; p++) *p = toupper(*p);
    sprintf(out, "%s / %d / %x / %5d|%-5d|", buf, 255, 255, 42, 42);
    puts(out);
    return 0;
}
