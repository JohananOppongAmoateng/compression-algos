#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void strip_newline(char *s) {
    size_t n = strlen(s);
    if (n > 0 && s[n - 1] == '\n')
        s[n - 1] = '\0';
}

static void rle_encode(const char *s) {
    if (*s == '\0') {
        printf("\n");
        return;
    }

    int first = 1;
    while (*s) {
        char c = *s;
        int count = 0;
        while (*s == c) {
            count++;
            s++;
        }
        if (!first)
            putchar(' ');
        first = 0;
        printf("%d%c", count, c);
    }
    putchar('\n');
}

static void rle_decode(const char *s) {
    while (*s) {
        while (*s == ' ')
            s++;
        if (*s == '\0')
            break;

        int count = 0;
        while (isdigit((unsigned char)*s)) {
            count = count * 10 + (*s - '0');
            s++;
        }
        if (*s == '\0')
            break;

        char c = *s++;
        for (int i = 0; i < count; i++)
            putchar(c);
    }
    putchar('\n');
}

int main(void) {
    char line[1024];
    while (fgets(line, sizeof line, stdin)) {
        strip_newline(line);
        if (line[0] == '\0')
            continue;

        if (strncmp(line, "ENCODE ", 7) == 0)
            rle_encode(line + 7);
        else if (strncmp(line, "DECODE ", 7) == 0)
            rle_decode(line + 7);
    }
    return 0;
}
