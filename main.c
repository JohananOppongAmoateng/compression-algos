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

#define DEFLATE_SYMBOLS 516
#define HUFFMAN_NODES   (DEFLATE_SYMBOLS * 2 - 1)
#define MAX_CODE_LENGTH DEFLATE_SYMBOLS

typedef struct {
    size_t freq;
    size_t order;
    int symbol;
    int left;
    int right;
    int active;
} HuffNode;

typedef struct {
    int *data;
    size_t len;
    size_t cap;
} SymbolBuf;

static int symbol_push(SymbolBuf *buf, int symbol) {
    if (buf->len == buf->cap) {
        size_t ncap = buf->cap ? buf->cap * 2 : 64;
        int *ndata = realloc(buf->data, ncap * sizeof *ndata);

        if (!ndata)
            return 0;
        buf->data = ndata;
        buf->cap = ncap;
    }
    buf->data[buf->len++] = symbol;
    return 1;
}

static int pick_huffman_node(const HuffNode *nodes, int count) {
    int best = -1;
    int i;

    for (i = 0; i < count; i++) {
        if (!nodes[i].active)
            continue;
        if (best < 0 || nodes[i].freq < nodes[best].freq ||
            (nodes[i].freq == nodes[best].freq &&
             nodes[i].order < nodes[best].order))
            best = i;
    }
    return best;
}

static void assign_huffman_codes(const HuffNode *nodes, int node,
                                 char *path, size_t depth,
                                 char codes[DEFLATE_SYMBOLS][MAX_CODE_LENGTH + 1]) {
    if (nodes[node].symbol >= 0) {
        if (depth == 0)
            path[depth++] = '0';
        path[depth] = '\0';
        strcpy(codes[nodes[node].symbol], path);
        return;
    }

    path[depth] = '0';
    assign_huffman_codes(nodes, nodes[node].left, path, depth + 1, codes);
    path[depth] = '1';
    assign_huffman_codes(nodes, nodes[node].right, path, depth + 1, codes);
}

int deflate_pipeline_main(void) {
    unsigned char *data = NULL;
    size_t len = 0, cap = 0;
    SymbolBuf stream = {NULL, 0, 0};
    size_t frequencies[DEFLATE_SYMBOLS] = {0};
    HuffNode nodes[HUFFMAN_NODES];
    char codes[DEFLATE_SYMBOLS][MAX_CODE_LENGTH + 1] = {{0}};
    char path[MAX_CODE_LENGTH + 1];
    size_t pos;
    size_t order = 0;
    size_t encoded_bits = 0;
    int node_count = 0;
    int active_count;
    int root;
    int symbol;
    int ch;

    while ((ch = getchar()) != EOF) {
        if (len == cap) {
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

    pos = 0;
    while (pos < len) {
        size_t best_offset = 0;
        size_t best_length = 0;
        size_t max_offset = pos < 15 ? pos : 15;
        size_t offset;

        for (offset = 1; offset <= max_offset; offset++) {
            size_t match_length = 0;

            while (match_length < 15 && pos + match_length < len &&
                   data[pos + match_length] ==
                       data[pos + match_length - offset])
                match_length++;

            if (match_length > best_length) {
                best_length = match_length;
                best_offset = offset;
            }
        }

        if (best_length >= 3) {
            if (!symbol_push(&stream, 256 + (int)best_length) ||
                !symbol_push(&stream, 300 + (int)best_offset)) {
                free(stream.data);
                free(data);
                return 1;
            }
            pos += best_length;
        } else {
            if (!symbol_push(&stream, data[pos])) {
                free(stream.data);
                free(data);
                return 1;
            }
            pos++;
        }
    }

    if (!symbol_push(&stream, 256)) {
        free(stream.data);
        free(data);
        return 1;
    }

    for (pos = 0; pos < stream.len; pos++)
        frequencies[stream.data[pos]]++;

    for (symbol = 0; symbol < DEFLATE_SYMBOLS; symbol++) {
        if (frequencies[symbol] == 0)
            continue;
        nodes[node_count].freq = frequencies[symbol];
        nodes[node_count].order = order++;
        nodes[node_count].symbol = symbol;
        nodes[node_count].left = -1;
        nodes[node_count].right = -1;
        nodes[node_count].active = 1;
        node_count++;
    }

    active_count = node_count;
    while (active_count > 1) {
        int left = pick_huffman_node(nodes, node_count);
        int right;

        nodes[left].active = 0;
        right = pick_huffman_node(nodes, node_count);
        nodes[right].active = 0;

        nodes[node_count].freq = nodes[left].freq + nodes[right].freq;
        nodes[node_count].order = order++;
        nodes[node_count].symbol = -1;
        nodes[node_count].left = left;
        nodes[node_count].right = right;
        nodes[node_count].active = 1;
        node_count++;
        active_count--;
    }

    root = pick_huffman_node(nodes, node_count);
    assign_huffman_codes(nodes, root, path, 0, codes);

    printf("codes:\n");
    for (symbol = 0; symbol < DEFLATE_SYMBOLS; symbol++) {
        if (frequencies[symbol] > 0) {
            printf("  %d: %s\n", symbol, codes[symbol]);
            encoded_bits += frequencies[symbol] * strlen(codes[symbol]);
        }
    }
    printf("encoded_bits=%zu\n", encoded_bits);
    for (pos = 0; pos < stream.len; pos++)
        fputs(codes[stream.data[pos]], stdout);
    putchar('\n');

    free(stream.data);
    free(data);
    return 0;
}

int crc32_main(void) {
    uint32_t crc = UINT32_C(0xffffffff);
    int byte;

    while ((byte = getchar()) != EOF) {
        int bit;

        crc ^= (uint32_t)(unsigned char)byte;
        for (bit = 0; bit < 8; bit++) {
            if (crc & 1u)
                crc = (crc >> 1) ^ UINT32_C(0xedb88320);
            else
                crc >>= 1;
        }
    }

    crc ^= UINT32_C(0xffffffff);
    printf("%08x\n", (unsigned int)crc);
    return 0;
}

int roundtrip_compression_main(void) {
    unsigned char *data = NULL;
    unsigned char *decoded = NULL;
    char *bits = NULL;
    size_t len = 0, cap = 0;
    size_t decoded_len = 0, decoded_cap = 0;
    SymbolBuf stream = {NULL, 0, 0};
    size_t frequencies[DEFLATE_SYMBOLS] = {0};
    HuffNode nodes[HUFFMAN_NODES];
    char codes[DEFLATE_SYMBOLS][MAX_CODE_LENGTH + 1] = {{0}};
    char path[MAX_CODE_LENGTH + 1];
    size_t pos;
    size_t order = 0;
    size_t compressed_bits = 0;
    size_t bit_pos = 0;
    size_t pending_length = 0;
    int node_count = 0;
    int active_count;
    int root;
    int symbol;
    int saw_eob = 0;
    int valid = 1;
    int ch;

    while ((ch = getchar()) != EOF) {
        if (len == cap) {
            size_t ncap = cap ? cap * 2 : 4096;
            unsigned char *ndata = realloc(data, ncap);

            if (!ndata) {
                free(data);
                return 1;
            }
            data = ndata;
            cap = ncap;
        }
        data[len++] = (unsigned char)ch;
    }

    /* Use the wider LZ77 limits for the complete round-trip pipeline. */
    pos = 0;
    while (pos < len) {
        size_t max_offset = pos < 16 ? pos : 16;
        size_t best_offset = 0;
        size_t best_length = 0;
        size_t offset;

        for (offset = 1; offset <= max_offset; offset++) {
            size_t match_length = 0;

            while (match_length < 258 && pos + match_length < len &&
                   data[pos + match_length] ==
                       data[pos + match_length - offset])
                match_length++;

            if (match_length > best_length) {
                best_length = match_length;
                best_offset = offset;
            }
        }

        if (best_length >= 3) {
            if (!symbol_push(&stream, 256 + (int)best_length) ||
                !symbol_push(&stream, 300 + (int)best_offset))
                goto allocation_failure;
            pos += best_length;
        } else {
            if (!symbol_push(&stream, data[pos]))
                goto allocation_failure;
            pos++;
        }
    }
    if (!symbol_push(&stream, 256))
        goto allocation_failure;

    /* Build the Huffman tree and code table. */
    for (pos = 0; pos < stream.len; pos++)
        frequencies[stream.data[pos]]++;

    for (symbol = 0; symbol < DEFLATE_SYMBOLS; symbol++) {
        if (frequencies[symbol] == 0)
            continue;
        nodes[node_count].freq = frequencies[symbol];
        nodes[node_count].order = order++;
        nodes[node_count].symbol = symbol;
        nodes[node_count].left = -1;
        nodes[node_count].right = -1;
        nodes[node_count].active = 1;
        node_count++;
    }

    active_count = node_count;
    while (active_count > 1) {
        int left = pick_huffman_node(nodes, node_count);
        int right;

        nodes[left].active = 0;
        right = pick_huffman_node(nodes, node_count);
        nodes[right].active = 0;
        nodes[node_count].freq = nodes[left].freq + nodes[right].freq;
        nodes[node_count].order = order++;
        nodes[node_count].symbol = -1;
        nodes[node_count].left = left;
        nodes[node_count].right = right;
        nodes[node_count].active = 1;
        node_count++;
        active_count--;
    }

    root = pick_huffman_node(nodes, node_count);
    assign_huffman_codes(nodes, root, path, 0, codes);
    for (pos = 0; pos < stream.len; pos++)
        compressed_bits += strlen(codes[stream.data[pos]]);

    bits = malloc(compressed_bits + 1);
    if (!bits)
        goto allocation_failure;
    for (pos = 0; pos < stream.len; pos++) {
        size_t code_len = strlen(codes[stream.data[pos]]);

        memcpy(bits + bit_pos, codes[stream.data[pos]], code_len);
        bit_pos += code_len;
    }
    bits[bit_pos] = '\0';

    /* Huffman-decode symbols and immediately expand their LZ77 meaning. */
    if (nodes[root].symbol >= 0) {
        saw_eob = nodes[root].symbol == 256;
        valid = saw_eob;
    } else {
        int current = root;

        for (bit_pos = 0; bit_pos < compressed_bits && !saw_eob; bit_pos++) {
            current = bits[bit_pos] == '0'
                          ? nodes[current].left
                          : nodes[current].right;
            if (nodes[current].symbol >= 0) {
                size_t needed;

                symbol = nodes[current].symbol;
                current = root;
                if (symbol == 256) {
                    saw_eob = pending_length == 0;
                    if (!saw_eob)
                        valid = 0;
                    break;
                }

                if (pending_length == 0 && symbol <= 255) {
                    needed = decoded_len + 1;
                    if (needed > decoded_cap) {
                        size_t ncap = decoded_cap ? decoded_cap * 2 : 64;
                        unsigned char *ndata = realloc(decoded, ncap);

                        if (!ndata)
                            goto allocation_failure;
                        decoded = ndata;
                        decoded_cap = ncap;
                    }
                    decoded[decoded_len++] = (unsigned char)symbol;
                } else if (pending_length == 0 &&
                           symbol >= 259 && symbol <= 514) {
                    pending_length = (size_t)(symbol - 256);
                } else if (pending_length > 0 &&
                           symbol >= 301 && symbol <= 316) {
                    size_t offset = (size_t)(symbol - 300);
                    size_t i;

                    if (offset > decoded_len ||
                        pending_length > SIZE_MAX - decoded_len) {
                        valid = 0;
                        break;
                    }
                    needed = decoded_len + pending_length;
                    if (needed > decoded_cap) {
                        size_t ncap = decoded_cap ? decoded_cap : 64;

                        while (ncap < needed)
                            ncap *= 2;
                        unsigned char *ndata = realloc(decoded, ncap);

                        if (!ndata)
                            goto allocation_failure;
                        decoded = ndata;
                        decoded_cap = ncap;
                    }
                    for (i = 0; i < pending_length; i++) {
                        decoded[decoded_len] =
                            decoded[decoded_len - offset];
                        decoded_len++;
                    }
                    pending_length = 0;
                } else {
                    valid = 0;
                    break;
                }
            }
        }
    }

    valid = valid && saw_eob && decoded_len == len &&
            (len == 0 || memcmp(decoded, data, len) == 0);
    printf("input_bytes=%zu\n", len);
    printf("compressed_bits=%zu\n", compressed_bits);
    printf("ratio=%.1f%%\n",
           len ? (double)compressed_bits * 100.0 / ((double)len * 8.0)
               : 0.0);
    printf("roundtrip=%s\n", valid ? "OK" : "FAIL");

    free(bits);
    free(decoded);
    free(stream.data);
    free(data);
    return 0;

allocation_failure:
    free(bits);
    free(decoded);
    free(stream.data);
    free(data);
    return 1;
}

static const unsigned char *bwt_sort_data;
static size_t bwt_sort_length;

static int compare_rotations(const void *left, const void *right) {
    size_t a = *(const size_t *)left;
    size_t b = *(const size_t *)right;
    size_t i;

    for (i = 0; i < bwt_sort_length; i++) {
        unsigned char ca = bwt_sort_data[(a + i) % bwt_sort_length];
        unsigned char cb = bwt_sort_data[(b + i) % bwt_sort_length];

        if (ca < cb)
            return -1;
        if (ca > cb)
            return 1;
    }
    return a < b ? -1 : a > b;
}

int main(void) {
    unsigned char *data = NULL;
    unsigned char *last = NULL;
    unsigned char *restored_last = NULL;
    unsigned char *decoded = NULL;
    size_t *rotations = NULL;
    size_t *occurrence = NULL;
    unsigned int *mtf = NULL;
    size_t len = 0, cap = 0;
    size_t primary = 0;
    size_t counts[256] = {0};
    size_t starts[256];
    size_t seen[256] = {0};
    unsigned char alphabet[256];
    size_t i;
    int ch;
    int ok = 1;

    while ((ch = getchar()) != EOF) {
        if (len == cap) {
            size_t ncap = cap ? cap * 2 : 4096;
            unsigned char *ndata = realloc(data, ncap);

            if (!ndata) {
                free(data);
                return 1;
            }
            data = ndata;
            cap = ncap;
        }
        data[len++] = (unsigned char)ch;
    }

    if (len > 0) {
        if (len > SIZE_MAX / sizeof *rotations ||
            len > SIZE_MAX / sizeof *occurrence ||
            len > SIZE_MAX / sizeof *mtf)
            goto allocation_failure;

        rotations = malloc(len * sizeof *rotations);
        last = malloc(len);
        restored_last = malloc(len);
        decoded = malloc(len);
        occurrence = malloc(len * sizeof *occurrence);
        mtf = malloc(len * sizeof *mtf);
        if (!rotations || !last || !restored_last || !decoded ||
            !occurrence || !mtf)
            goto allocation_failure;

        for (i = 0; i < len; i++)
            rotations[i] = i;
        bwt_sort_data = data;
        bwt_sort_length = len;
        qsort(rotations, len, sizeof *rotations, compare_rotations);

        for (i = 0; i < len; i++) {
            size_t start = rotations[i];

            last[i] = data[(start + len - 1) % len];
            if (start == 0)
                primary = i;
        }
    }

    /* Move-to-front encode the BWT last column. */
    for (i = 0; i < 256; i++)
        alphabet[i] = (unsigned char)i;
    for (i = 0; i < len; i++) {
        unsigned int index = 0;
        unsigned char value;

        while (alphabet[index] != last[i])
            index++;
        mtf[i] = index;
        value = alphabet[index];
        while (index > 0) {
            alphabet[index] = alphabet[index - 1];
            index--;
        }
        alphabet[0] = value;
    }

    /* Decode MTF back to the last column before applying inverse BWT. */
    for (i = 0; i < 256; i++)
        alphabet[i] = (unsigned char)i;
    for (i = 0; i < len; i++) {
        unsigned int index = mtf[i];
        unsigned char value = alphabet[index];

        restored_last[i] = value;
        while (index > 0) {
            alphabet[index] = alphabet[index - 1];
            index--;
        }
        alphabet[0] = value;
    }

    /*
     * Build the LF mapping. occurrence[i] is the rank of last[i] among
     * equal bytes; starts[b] is the first row beginning with byte b.
     */
    for (i = 0; i < len; i++) {
        occurrence[i] = seen[restored_last[i]]++;
        counts[restored_last[i]]++;
    }
    starts[0] = 0;
    for (i = 1; i < 256; i++)
        starts[i] = starts[i - 1] + counts[i - 1];

    if (len > 0) {
        size_t row = primary;

        for (i = len; i-- > 0;) {
            unsigned char value = restored_last[row];

            decoded[i] = value;
            row = starts[value] + occurrence[row];
        }
    }

    ok = len == 0 || memcmp(decoded, data, len) == 0;
    printf("bwt_primary=%zu\n", primary);
    printf("bwt_last=");
    for (i = 0; i < len; i++)
        printf("%02x", (unsigned int)last[i]);
    printf("\nmtf=");
    for (i = 0; i < len; i++) {
        if (i > 0)
            putchar(',');
        printf("%u", mtf[i]);
    }
    printf("\nroundtrip=%s\n", ok ? "OK" : "FAIL");

    free(mtf);
    free(occurrence);
    free(decoded);
    free(restored_last);
    free(last);
    free(rotations);
    free(data);
    return 0;

allocation_failure:
    free(mtf);
    free(occurrence);
    free(decoded);
    free(restored_last);
    free(last);
    free(rotations);
    free(data);
    return 1;
}
