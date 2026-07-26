#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SYMBOLS 256
#define MAX_BITS 16
#define MAX_LINE 4096

typedef struct {
    int symbol;
    int length;
    unsigned int code;
} Entry;

int cmp_length_then_symbol(const void *a, const void *b) {
    const Entry *ea = a, *eb = b;
    if (ea->length != eb->length)
        return ea->length - eb->length;
    return ea->symbol - eb->symbol;
}

/* Format `code` as `length` bits into buf (NUL-terminated). */
void format_code(unsigned int code, int length, char *buf) {
    int i;
    for (i = 0; i < length; i++)
        buf[i] = (code & (1u << (length - 1 - i))) ? '1' : '0';
    buf[length] = '\0';
}

void strip_newline(char *s) {
    size_t n = strlen(s);
    if (n > 0 && s[n - 1] == '\n')
        s[n - 1] = '\0';
}

int main(void) {
    Entry entries[MAX_SYMBOLS];
    Entry sorted[MAX_SYMBOLS];
    int n = 0;
    int i;
    int bl_count[MAX_BITS + 1] = {0};
    unsigned int next_code[MAX_BITS + 1] = {0};
    int max_len = 0;
    char line[MAX_LINE];
    char bits[MAX_BITS + 1];

    while (fgets(line, sizeof line, stdin)) {
        int symbol, length;
        strip_newline(line);
        if (line[0] == '\0')
            continue;
        if (sscanf(line, "%d %d", &symbol, &length) != 2)
            return 1;
        if (symbol < 0 || symbol > 255 || length < 0 || length > MAX_BITS)
            return 1;
        if (n >= MAX_SYMBOLS)
            return 1;
        entries[n].symbol = symbol;
        entries[n].length = length;
        entries[n].code = 0;
        n++;
    }

    for (i = 0; i < n; i++) {
        if (entries[i].length > 0)
            bl_count[entries[i].length]++;
        if (entries[i].length > max_len)
            max_len = entries[i].length;
    }

    /* RFC 1951 §3.2.2 step 2 */
    next_code[1] = 0;
    for (i = 2; i <= max_len; i++)
        next_code[i] = (next_code[i - 1] + (unsigned int)bl_count[i - 1]) << 1;

    /* Step 3: assign in (length, symbol) order */
    memcpy(sorted, entries, (size_t)n * sizeof(Entry));
    qsort(sorted, (size_t)n, sizeof(Entry), cmp_length_then_symbol);

    for (i = 0; i < n; i++) {
        int len = sorted[i].length;
        unsigned int code;
        int j;

        if (len == 0)
            continue;
        code = next_code[len]++;
        /* Write code back onto the matching original entry. */
        for (j = 0; j < n; j++) {
            if (entries[j].symbol == sorted[i].symbol &&
                entries[j].length == len) {
                entries[j].code = code;
                break;
            }
        }
    }

    for (i = 0; i < n; i++) {
        format_code(entries[i].code, entries[i].length, bits);
        if (i + 1 < n)
            printf("%d %s\n", entries[i].symbol, bits);
        else
            printf("%d %s", entries[i].symbol, bits);
    }

    return 0;
}
