#ifndef MEMORY_H
#define MEMORY_H

#include "defs.h"
#include "token.h"

#define MEMORY_SIZE        32*1024

struct memory {
    byte  *mem;
    byte  *cur;
    size_t sz;
};

int check_mem(struct token tok);

#endif
