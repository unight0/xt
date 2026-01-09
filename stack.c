#include "stack.h"

void error(struct token t, const char *msg);
void throw(struct token, cell);

struct stack
new_stack(struct memory *mem, size_t size) {
    struct stack st = (struct stack) {
        mem->cur + size,
        mem->cur + size,
        mem->cur,
    };

    mem->cur += size;

    return st;
}


void
push(struct token   t,
     struct stack  *st,
     cell           c) {
    if (st->cur - sizeof(cell) <= st->end) {
        error(t, "Stack overflow!");
        throw(t, THROW_STACK_OVERFLOW);
        return;
    }
    st->cur -= sizeof(cell);
    *(cell*) st->cur = c;
}

cell
top(struct token   t,
    struct stack  *st) {
    if (st->cur == st->begin) {
        error(t, "Stack underflow!");
        throw(t, THROW_STACK_UNDERFLOW);
        return 0;
    }
    cell c = *(cell*) st->cur;
    return c;
}

cell
pop(struct token   t,
    struct stack  *st) {
    if (st->cur == st->begin) {
        error(t, "Stack underflow!");
        throw(t, THROW_STACK_UNDERFLOW);
        return 0;
    }
    cell c = *(cell*) st->cur;
    st->cur += sizeof(cell);
    return c;
}
