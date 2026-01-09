#ifndef STACK_H
#define STACK_H

#include "defs.h"
#include "token.h"
#include "memory.h"

#define STACK_SIZE         256
#define RSTACK_SIZE        512

// Note that stack grows downwards
struct stack {
    byte *begin;
    byte *cur;
    byte *end;
};

struct stack new_stack(struct memory *mem, size_t size);
void push(struct token t, struct stack *st, cell c);
cell top(struct token t, struct stack *st);
cell pop(struct token t, struct stack *st);

#endif
