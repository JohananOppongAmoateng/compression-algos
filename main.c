#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NODES 512
#define MAX_CODE 256
#define MAX_LINE 4096

typedef struct Node {
    unsigned char byte;
    int is_leaf;
    struct Node *left;
    struct Node *right;
} Node;

Node nodes[MAX_NODES];
int node_count;

Node *new_node(void) {
    Node *n = &nodes[node_count++];
    n->byte = 0;
    n->is_leaf = 0;
    n->left = NULL;
    n->right = NULL;
    return n;
}

/* Insert a code into the trie. Returns 0 on success, -1 on conflict. */
int insert_code(Node *root, unsigned char symbol, const char *code) {
    Node *cur = root;
    size_t i, len = strlen(code);

    if (len == 0)
        return -1;

    for (i = 0; i < len; i++) {
        char bit = code[i];
        Node **child;

        if (bit != '0' && bit != '1')
            return -1;
        if (cur->is_leaf)
            return -1; /* prefix conflict */

        child = (bit == '0') ? &cur->left : &cur->right;
        if (!*child)
            *child = new_node();
        cur = *child;
    }

    if (cur->is_leaf || cur->left || cur->right)
        return -1; /* duplicate or prefix of existing */
    cur->is_leaf = 1;
    cur->byte = symbol;
    return 0;
}

void decode(Node *root, const char *bits) {
    Node *state = root;
    size_t i, len = strlen(bits);
    char *out;
    size_t out_len = 0;

    out = malloc(len + 1);
    if (!out) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }

    for (i = 0; i < len; i++) {
        char bit = bits[i];

        if (bit != '0' && bit != '1') {
            free(out);
            fputs("ERR truncated", stdout);
            return;
        }

        state = (bit == '0') ? state->left : state->right;
        if (!state) {
            free(out);
            fputs("ERR truncated", stdout);
            return;
        }
        if (state->is_leaf) {
            out[out_len++] = (char)state->byte;
            state = root;
        }
    }

    if (state != root) {
        free(out);
        fputs("ERR truncated", stdout);
        return;
    }

    fwrite(out, 1, out_len, stdout);
    free(out);
}

void strip_newline(char *s) {
    size_t n = strlen(s);
    if (n > 0 && s[n - 1] == '\n')
        s[n - 1] = '\0';
}

int main(void) {
    char line[MAX_LINE];
    int n, i;
    Node *root;

    if (!fgets(line, sizeof line, stdin))
        return 0;
    if (sscanf(line, "%d", &n) != 1 || n < 0)
        return 1;

    node_count = 0;
    root = new_node();

    for (i = 0; i < n; i++) {
        int symbol;
        char code[MAX_CODE];

        if (!fgets(line, sizeof line, stdin))
            return 1;
        if (sscanf(line, "%d %255s", &symbol, code) != 2)
            return 1;
        if (symbol < 0 || symbol > 255)
            return 1;
        if (insert_code(root, (unsigned char)symbol, code) != 0)
            return 1;
    }

    if (!fgets(line, sizeof line, stdin)) {
        decode(root, "");
        return 0;
    }
    strip_newline(line);
    decode(root, line);
    return 0;
}
