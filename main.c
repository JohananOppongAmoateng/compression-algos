#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define CODE_BITS 32
#define HALF       (1u << (CODE_BITS - 1))
#define FIRST_QTR  (HALF >> 1)
#define THIRD_QTR  (HALF + FIRST_QTR)
#define TOP        (0xffffffffu)
#define EOF_SYM    256
#define MAX_SYMS   257

typedef struct {
    int freq[MAX_SYMS];
    int low_count[MAX_SYMS];
    int high_count[MAX_SYMS];
    int symbols[MAX_SYMS];
    int nsyms;
    int total;
} Model;

void build_model(Model *m, const unsigned char *data, size_t len) {
    int i, c;

    memset(m, 0, sizeof *m);
    for (i = 0; (size_t)i < len; i++)
        m->freq[data[i]]++;
    m->freq[EOF_SYM] = 1;

    c = 0;
    m->nsyms = 0;
    for (i = 0; i < MAX_SYMS; i++) {
        if (m->freq[i] == 0)
            continue;
        m->symbols[m->nsyms++] = i;
        m->low_count[i] = c;
        c += m->freq[i];
        m->high_count[i] = c;
    }
    m->total = c;
}

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} BitBuf;

void bitbuf_init(BitBuf *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void bitbuf_push(BitBuf *b, int bit) {
    if (b->len + 1 >= b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 64;
        char *nd = realloc(b->data, ncap);
        if (!nd) {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
        b->data = nd;
        b->cap = ncap;
    }
    b->data[b->len++] = bit ? '1' : '0';
    b->data[b->len] = '\0';
}

void bit_plus_follow(BitBuf *b, int bit, int *pending) {
    bitbuf_push(b, bit);
    while (*pending > 0) {
        bitbuf_push(b, 1 - bit);
        (*pending)--;
    }
}

char *encode(const unsigned char *data, size_t len, Model *m) {
    BitBuf out;
    uint32_t low = 0, high = TOP;
    int pending = 0;
    size_t i;
    int s;

    build_model(m, data, len);
    bitbuf_init(&out);

    for (i = 0; i <= len; i++) {
        uint64_t rng;
        s = (i < len) ? data[i] : EOF_SYM;
        rng = (uint64_t)high - low + 1;
        high = (uint32_t)(low + rng * (uint64_t)m->high_count[s] / (uint64_t)m->total - 1);
        low = (uint32_t)(low + rng * (uint64_t)m->low_count[s] / (uint64_t)m->total);

        for (;;) {
            if (high < HALF) {
                bit_plus_follow(&out, 0, &pending);
            } else if (low >= HALF) {
                bit_plus_follow(&out, 1, &pending);
                low -= HALF;
                high -= HALF;
            } else if (low >= FIRST_QTR && high < THIRD_QTR) {
                pending++;
                low -= FIRST_QTR;
                high -= FIRST_QTR;
            } else {
                break;
            }
            low = low * 2;
            high = high * 2 + 1;
        }
    }

    pending++;
    if (low < FIRST_QTR)
        bit_plus_follow(&out, 0, &pending);
    else
        bit_plus_follow(&out, 1, &pending);

    return out.data;
}

int find_symbol(const Model *m, int cum) {
    int i;
    for (i = 0; i < m->nsyms; i++) {
        int s = m->symbols[i];
        if (m->low_count[s] <= cum && cum < m->high_count[s])
            return s;
    }
    return -1;
}

unsigned char *decode(const char *bits, size_t nbits, const Model *m, size_t *out_len) {
    size_t idx = 0;
    uint32_t value = 0;
    uint32_t low = 0, high = TOP;
    unsigned char *out = NULL;
    size_t olen = 0, ocap = 0;
    int i;

    for (i = 0; i < CODE_BITS; i++) {
        int b = 0;
        if (idx < nbits)
            b = bits[idx++] == '1';
        value = (value << 1) | (uint32_t)b;
    }

    for (;;) {
        uint64_t rng = (uint64_t)high - low + 1;
        int cum = (int)((((uint64_t)value - low + 1) * (uint64_t)m->total - 1) / rng);
        int s = find_symbol(m, cum);

        if (s < 0) {
            free(out);
            *out_len = 0;
            return NULL;
        }
        if (s == EOF_SYM)
            break;

        if (olen + 1 > ocap) {
            size_t ncap = ocap ? ocap * 2 : 64;
            unsigned char *nd = realloc(out, ncap);
            if (!nd) {
                free(out);
                fprintf(stderr, "out of memory\n");
                exit(1);
            }
            out = nd;
            ocap = ncap;
        }
        out[olen++] = (unsigned char)s;

        high = (uint32_t)(low + rng * (uint64_t)m->high_count[s] / (uint64_t)m->total - 1);
        low = (uint32_t)(low + rng * (uint64_t)m->low_count[s] / (uint64_t)m->total);

        for (;;) {
            if (high < HALF) {
                /* nothing */
            } else if (low >= HALF) {
                value -= HALF;
                low -= HALF;
                high -= HALF;
            } else if (low >= FIRST_QTR && high < THIRD_QTR) {
                value -= FIRST_QTR;
                low -= FIRST_QTR;
                high -= FIRST_QTR;
            } else {
                break;
            }
            {
                int b = 0;
                if (idx < nbits)
                    b = bits[idx++] == '1';
                low = low * 2;
                high = high * 2 + 1;
                value = (value << 1) | (uint32_t)b;
            }
        }
    }

    *out_len = olen;
    return out;
}

int main(void) {
    unsigned char *data = NULL;
    size_t len = 0, cap = 0;
    int ch;
    Model model;
    char *bits;
    unsigned char *decoded;
    size_t decoded_len;
    int ok;

    while ((ch = getchar()) != EOF) {
        if (len + 1 > cap) {
            size_t ncap = cap ? cap * 2 : 4096;
            unsigned char *nd = realloc(data, ncap);
            if (!nd) {
                free(data);
                return 1;
            }
            data = nd;
            cap = ncap;
        }
        data[len++] = (unsigned char)ch;
    }

    bits = encode(data, len, &model);
    decoded = decode(bits, strlen(bits), &model, &decoded_len);
    ok = decoded && decoded_len == len &&
         (len == 0 || memcmp(decoded, data, len) == 0);

    printf("bits=%zu\n%s\nok=%s", strlen(bits), bits, ok ? "OK" : "FAIL");

    free(bits);
    free(decoded);
    free(data);
    return 0;
}
