#ifndef INPUT_H
#define INPUT_H

#include "defs.h"

#define SOURCE_SIZE        1024

struct input {
    byte  source[SOURCE_SIZE];
    int   source_file;
    int   line;
    int   col;
    byte *cur;
};

struct inputs {
    size_t num;
    struct input inps[];
};

struct inputs *inputs_append(struct inputs *inps, struct input inp);
struct inputs *inputs_pop(struct inputs *inps);
struct input  *inputs_top(struct inputs *inps);

#endif
