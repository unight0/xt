#include "input.h"

#include <stdio.h>

struct inputs *
inputs_append(struct inputs *inps, struct input inp) {
    inps->num++;
    inps = realloc(inps,
                sizeof(struct inputs) +
                //sizeof(size_t) +
                sizeof(struct input) * inps->num);
    if (inps == NULL) {
        perror("realloc()");
        exit(1);
    }
    inps->inps[inps->num - 1] = inp;
    return inps;
}

struct inputs *
inputs_pop(struct inputs *inps) {
    inps->num--;
    inps = realloc(inps,
            sizeof(struct inputs) + 
            //sizeof(size_t) +
            sizeof(struct input) * inps->num);
    if (inps == NULL) {
        perror("realloc()");
        exit(1);
    }
    return inps;
}

struct input *
inputs_top(struct inputs *inps) {
    if (inps->num == 0) return NULL;
    return &inps->inps[inps->num - 1];
}
