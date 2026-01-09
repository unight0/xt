#include "lexer.h"

#include <ctype.h>
#include <string.h>
#include <assert.h>

void
advance(struct input *st) {
    //assert(*st->cur && "Cannot advance past the end of string");
    st->col++;
    if (*st->cur == '\n') {
        st->col = 0;
        st->line++;
    }
    st->cur++;
}

struct token
next(struct input *st) {
    if (!*st->cur) return (struct token) {0, 0, NULL};

    while (isspace(*st->cur) && *st->cur) {
        advance(st);
    }
    char *val = (char*)st->cur;
    const int line = st->line;
    const int col = st->col;

    while (!isspace(*st->cur) && *st->cur) {
        advance(st);
    }

    char *end = (char*)st->cur;

    if (*end) {
        advance(st);
        *end = 0;
    }

    if (!strlen(val)) return (struct token) {line, col, NULL};

    return (struct token) {line, col, val};
}



int
identify(char *tok) {
    assert(strlen(tok) && "Empty token");

    // 'c'
    if (strlen(tok) == 3) {
        if (tok[0] == '\'' && tok[2] == '\'') {
            return ID_CHAR;
        }
    }

    // 0xDEADBEEF
    if (*tok == '0' && (tok[1] == 'x' || tok[1] == 'X')) {
        tok += 2;
        for (; isxdigit(*tok); tok++);
        if (!*tok) return ID_HEX;
        return ID_WORD;
    }

    // 0b1111101111101
    if (*tok == '0' && (tok[1] == 'b' || tok[1] == 'B')) {
        tok += 2;
        for (; isxdigit(*tok); tok++);
        if (!*tok) return ID_HEX;
        return ID_WORD;
    }

    if ((*tok == '-') && strlen(tok) > 1)
        tok++;

    if (!isdigit(*tok)) return ID_WORD;

    int dots = 0;

    for (; isdigit(*tok) || *tok == '.'; tok++) {
        if (*tok == '.') dots++;
    }
    if (*tok) return ID_WORD;

    if (dots > 1) return ID_WORD;
    if (dots == 1) return ID_FLT;
    return ID_INT;
}

