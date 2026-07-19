#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TODO (frequency): implement per the lesson description. */

int main(void) {
    char line[1024];
    while (fgets(line, sizeof line, stdin)) {
        if (line[0] == '\n' || line[0] == 0) continue;
        printf("TODO\n");
    }
    return 0;
}
