#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NODES 512
#define MAX_CODE 256

typedef struct Node {
    int freq;
    unsigned char byte; /* symbol for leaves; min byte in subtree for internals */
    int is_leaf;
    int seq;
    struct Node *left;
    struct Node *right;
} Node;

static Node nodes[MAX_NODES];
static int node_count;
static int seq_counter;

static Node *heap[MAX_NODES];
static int heap_size;

static int node_less(const Node *a, const Node *b) {
    if (a->freq != b->freq)
        return a->freq < b->freq;
    if (a->byte != b->byte)
        return a->byte < b->byte;
    if (a->is_leaf != b->is_leaf)
        return a->is_leaf > b->is_leaf; /* prefer leaves */
    return a->seq < b->seq;
}

static void heap_swap(int i, int j) {
    Node *t = heap[i];
    heap[i] = heap[j];
    heap[j] = t;
}

static void heap_up(int i) {
    while (i > 0) {
        int p = (i - 1) / 2;
        if (!node_less(heap[i], heap[p]))
            break;
        heap_swap(i, p);
        i = p;
    }
}

static void heap_down(int i) {
    for (;;) {
        int l = 2 * i + 1, r = 2 * i + 2, best = i;
        if (l < heap_size && node_less(heap[l], heap[best]))
            best = l;
        if (r < heap_size && node_less(heap[r], heap[best]))
            best = r;
        if (best == i)
            break;
        heap_swap(i, best);
        i = best;
    }
}

static void heap_push(Node *n) {
    heap[heap_size] = n;
    heap_up(heap_size);
    heap_size++;
}

static Node *heap_pop(void) {
    Node *n = heap[0];
    heap_size--;
    if (heap_size > 0) {
        heap[0] = heap[heap_size];
        heap_down(0);
    }
    return n;
}

static Node *new_node(int freq, unsigned char byte, int is_leaf,
                      Node *left, Node *right) {
    Node *n = &nodes[node_count++];
    n->freq = freq;
    n->byte = byte;
    n->is_leaf = is_leaf;
    n->seq = seq_counter++;
    n->left = left;
    n->right = right;
    return n;
}

static char codes[256][MAX_CODE];
static int has_code[256];

static void assign_codes(Node *n, char *path, int depth) {
    if (n->is_leaf) {
        if (depth == 0) {
            codes[n->byte][0] = '0';
            codes[n->byte][1] = '\0';
        } else {
            path[depth] = '\0';
            memcpy(codes[n->byte], path, (size_t)depth + 1);
        }
        has_code[n->byte] = 1;
        return;
    }
    path[depth] = '0';
    assign_codes(n->left, path, depth + 1);
    path[depth] = '1';
    assign_codes(n->right, path, depth + 1);
}

static void print_byte_repr(unsigned char b) {
    /* Same convention as frequency lesson: printable as-is. */
    if (b >= 32 && b < 127 && b != ' ')
        putchar((char)b);
    else
        printf("\\x%02x", b);
}

static void huffman_codes(const unsigned char *data, size_t len) {
    int freq[256] = {0};
    size_t i;

    node_count = 0;
    seq_counter = 0;
    heap_size = 0;
    memset(has_code, 0, sizeof has_code);

    for (i = 0; i < len; i++)
        freq[data[i]]++;

    for (i = 0; i < 256; i++) {
        if (freq[i] > 0)
            heap_push(new_node(freq[i], (unsigned char)i, 1, NULL, NULL));
    }

    if (heap_size == 0)
        return;

    if (heap_size == 1) {
        Node *only = heap_pop();
        print_byte_repr(only->byte);
        printf(" 0\n");
        return;
    }

    while (heap_size > 1) {
        Node *a = heap_pop();
        Node *b = heap_pop();
        unsigned char mb = a->byte < b->byte ? a->byte : b->byte;
        heap_push(new_node(a->freq + b->freq, mb, 0, a, b));
    }

    {
        char path[MAX_CODE];
        assign_codes(heap_pop(), path, 0);
    }

    for (i = 0; i < 256; i++) {
        if (has_code[i]) {
            print_byte_repr((unsigned char)i);
            printf(" %s\n", codes[i]);
        }
    }
}

static void strip_newline(char *s) {
    size_t n = strlen(s);
    if (n > 0 && s[n - 1] == '\n')
        s[n - 1] = '\0';
}

int main(void) {
    char line[4096];
    while (fgets(line, sizeof line, stdin)) {
        strip_newline(line);
        if (line[0] == '\0')
            continue;
        huffman_codes((const unsigned char *)line, strlen(line));
    }
    return 0;
}
