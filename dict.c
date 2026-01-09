#include "dict.h"

#include <string.h>
#include <stdio.h>

struct entry *
dict_search(struct entry *dict, const char *word) {
    for (size_t i = 0; i < 1000000; i++) {
        if (dict == NULL) return NULL;
        if (!strcmp(dict->name, word))
            return dict;
        dict = dict->next;
    }
    printf("CRITICAL ERROR: Either dictionary is looped, or it has over 1_000_000 entries\n");
    exit(1);
}

struct entry *
dict_append_builtin(struct memory *mem, struct entry *dict, char *name, byte flags, builtin b) {
    struct entry *entry = (struct entry*) mem->cur;

    *entry = (struct entry) {
        dict,
        name,
        flags,
        b,
        NULL
    };

    mem->cur += sizeof(struct entry);

    return entry;
}

struct entry *
dict_append(struct memory *mem, struct entry *dict, char *name, byte flags, byte *body, builtin b) {
    struct entry *entry = (struct entry*) mem->cur;

    *entry = (struct entry) {
        dict,
        name,
        flags,
        b,
        body
    };

    mem->cur += sizeof(struct entry);

    return entry;
}

