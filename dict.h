#ifndef DICT_H
#define DICT_H

#include "defs.h"
#include "memory.h"
#include "token.h"

#define DICT_FLAG_IMMED    1
#define DICT_FLAG_COMPONLY 1 << 1
#define DICT_FLAG_INTRONLY 1 << 2

typedef void (builtin (struct token));

struct entry {
    struct entry *next;
    char         *name;
    byte          flags;
    builtin      *code;
    byte         *body;
};

struct entry *dict_search(struct entry *dict, const char *word);
struct entry *dict_append_builtin(struct memory *mem, struct entry *dict, char *name, byte flags, builtin b);
struct entry *dict_append(struct memory *mem, struct entry *dict, char *name, byte flags, byte *body, builtin b);

#endif
