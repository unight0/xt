#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>
#include <signal.h>

#define DICT_FLAG_IMMED    1
#define DICT_FLAG_COMPONLY 1 << 1
#define DICT_FLAG_INTRONLY 1 << 2
#define MEMORY_SIZE        32*1024
#define STACK_SIZE         256
#define RSTACK_SIZE        512
#define PAD_SIZE           1024
#define SOURCE_SIZE        1024
#define MODE_INTERPRET     0
#define MODE_COMPILE       1
#define ID_WORD            0
#define ID_INT             1
#define ID_HEX             2
#define ID_BIN             3
#define ID_FLT             4
#define ID_CHAR            5

#define THROW_STACK_OVERFLOW   -3
#define THROW_STACK_UNDERFLOW  -4
//#define THROW_RSTACK_OVERFLOW  -5
//#define THROW_RSTACK_UNDERFLOW -6
#define THROW_UNDEFINED_WORD   -13
#define THROW_COMPONLY_WORD    -14
#define THROW_EOF              -39
#define THROW_FILE_NONEXISTENT -996
#define THROW_INVALID_ARGUMENT -997
#define THROW_INTRONLY_WORD    -998
#define THROW_OUT_OF_MEM       -999

typedef uint64_t cell;
typedef uint8_t  byte;

// TODO: move away from FILE* to fds
// + Some things just translate directly
// + Can actually check if fd is valid,
//   invalid FILE* just point ot random
//   memory, thus are unverifiable and 
//   ultimately lead to segfaults
// - Cannot use fputs for refilling
struct input {
    char  source[SOURCE_SIZE];
    FILE *source_file;
    int   line;
    int   col;
    char *cur;
};

struct inputs {
    size_t num;
    struct input inps[];
};

struct token {
    int   line;
    int   col;
    char *val;
};

struct memory {
    char  *mem;
    char  *cur;
    size_t sz;
};

// Note that stack grows downwards
struct stack {
    char *begin;
    char *cur;
    char *end;
};

struct environ;
typedef void (builtin (struct environ en, struct token));

struct entry {
    struct entry *next;
    char         *name;
    byte          flags;
    builtin      *code;
    char         *body;
};

// This is a huge struct that is passed by value.
// Besides, we have so many double pointers that it
// would make sense to just pass the *pointer* to environment
// and story single pointers within the struct.
// But it does require a lot of fixes...

// TODO (1): pass environment by pointer
// TODO (2) make stuff global?

struct environ {
    struct inputs **inps;
    struct input  **inpt;
    struct memory *mem;
    struct stack  *st;
    struct stack  *rst;
    struct entry  **dict;
    char          *mode;
    char          *pad;
    /* Used for compilation mode */
    char          **newword_name;
    char          **newword;
    //size_t        *newword_sz;
    /* Inner interpreter */
    cell         **ip;
    struct entry **xt; 
    cell         **catch;
    char         **catch_st;
    char         **catch_rst;
    char          *terminate;
};

void throw(struct environ, struct token, cell);

//struct inputs {
//    size_t num;
//    struct input inps[];
//};

struct inputs *
inputs_append(struct inputs *inps, struct input inp) {
    inps->num++;
    inps = realloc(inps,
                sizeof(struct inputs) +
                //sizeof(size_t) +
                sizeof(struct input) * inps->num);
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
    return inps;
}

struct input *
inputs_top(struct inputs *inps) {
    if (inps->num == 0) return NULL;
    return &inps->inps[inps->num - 1];
}

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
    char *val = st->cur;
    const int line = st->line;
    const int col = st->col;

    while (!isspace(*st->cur) && *st->cur) {
        advance(st);
    }

    char *end = st->cur;

    if (*end) {
        advance(st);
        *end = 0;
    }

    if (!strlen(val)) return (struct token) {line, col, NULL};

    return (struct token) {line, col, val};
}


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
dict_append(struct memory *mem, struct entry *dict, char *name, byte flags, char *body, builtin b) {
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

void
error(struct token t, const char *msg) {
    printf("Error at (%d:%d '%s'): %s\n", t.line + 1, t.col + 1, t.val, msg);
}

void
push(struct environ en,
     struct token   t,
     struct stack  *st,
     cell           c) {
    if (st->cur - sizeof(cell) <= st->end) {
        error(t, "Stack overflow!");
        throw(en, t, THROW_STACK_OVERFLOW);
        return;
    }
    st->cur -= sizeof(cell);
    *(cell*) st->cur = c;
}

cell
top(struct environ en,
    struct token   t,
    struct stack  *st) {
    if (st->cur == st->begin) {
        error(t, "Stack underflow!");
        throw(en, t, THROW_STACK_UNDERFLOW);
        return 0;
    }
    cell c = *(cell*) st->cur;
    return c;
}

cell
pop(struct environ en,
    struct token   t,
    struct stack  *st) {
    if (st->cur == st->begin) {
        error(t, "Stack underflow!");
        throw(en, t, THROW_STACK_UNDERFLOW);
        return 0;
    }
    cell c = *(cell*) st->cur;
    st->cur += sizeof(cell);
    return c;
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

int
check_mem(struct environ en, struct token tok) {
    if (en.mem->cur > en.mem->mem + en.mem->sz) {
        error(tok, "Out of memory!");
        en.mem->cur  = en.mem->mem + en.mem->sz;
        throw(en, tok, THROW_OUT_OF_MEM);
        return 1;
    }
    return 0;
}

void
NEXT(struct environ en,
     struct token   tok) {
    while (*en.ip != NULL) {
        *en.xt = (struct entry*)**en.ip;

        //printf("NEXT(): %s\n", (*en.xt)->name);

        if ((*en.xt)->flags & DICT_FLAG_INTRONLY) {
            error(tok, "INTERNAL CRITICAL ERROR: Encountered an interpret-only word during NEXT()");
            exit(1);
        }

        // It is ESSENTIAL that IP is incremented BEFORE code
        // section is called. This cost me 2+ hours of debugging!
        ++*en.ip;

        (*en.xt)->code(en, tok);

        // Sometimes ip is advanced when we THROW
        if (*en.ip < (cell*)(3 * 8)) break;
    }
}

// Entry into inner interpreter
void
execute_word(struct environ en,
             struct token   tok,
             struct entry  *e) {

    *en.xt = e;
    e->code(en, tok);
    NEXT(en, tok);

    // Catches set within the return stack cannot be returned to
    *en.catch = NULL;
    *en.catch_st = en.st->cur;
    *en.catch_rst = en.rst->cur;
}

void
execute_word_tok(struct environ en,
                 struct token tok) {
    struct entry *def = dict_search(*en.dict, tok.val);
    if (def == NULL) {
        error(tok, "Unknown word");
        throw(en, tok, THROW_UNDEFINED_WORD);
        return;
    }
    if (def->flags & DICT_FLAG_COMPONLY) {
        error(tok, "Word is compile-only");
        throw(en, tok, THROW_COMPONLY_WORD);
        return;
    }
    execute_word(en, tok, def);
}
void
execute_token(struct environ en,
              struct token tok) {

    int id = identify(tok.val);

    switch (id) {
        case ID_INT:
            push(en, tok, en.st, strtoll(tok.val, NULL, 10));
            break;
        case ID_HEX:
            push(en, tok, en.st, strtoll(tok.val, NULL, 16));
            break;
        case ID_BIN:
            push(en, tok, en.st, strtoll(tok.val, NULL, 2));
            break;
        case ID_CHAR:
            push(en, tok, en.st, (cell) tok.val[1]);
            break;
        case ID_FLT:
            printf("Floats are not implemented yet\n");
            exit(1);
        case ID_WORD:
            execute_word_tok(en, tok);
            break;
        default:
            printf("Token id: %d\n", id);
            assert(0 && "Unknown token type");
    }
}

void
compile_word(struct environ en,
             struct token   tok) {
    struct entry *e = dict_search(*en.dict, tok.val);
    if (e == NULL) {
        error(tok, "Unknown word");
        throw(en, tok, THROW_UNDEFINED_WORD);
        return;
    }
    //printf("CW: %s\n", e->name);
    if (e->flags & DICT_FLAG_IMMED) {
        //printf("IMMED %s\n", e->name);
        execute_word(en, tok, e);
        return;
    }
    if (e->flags & DICT_FLAG_INTRONLY) {
        error(tok, "Word is interpret-only");
        throw(en, tok, THROW_INTRONLY_WORD);
        return;
    }
    *(cell*) en.mem->cur = (cell) e;
    en.mem->cur += sizeof(cell);
    //++*en.newword_sz;
}

void
compile_push(struct token   tok,
             struct environ en,
             cell           val) {

    struct entry *w_lit = dict_search(*en.dict, "lit");
    
    if (w_lit == NULL) {
        error(tok, "CRITICAL ERROR: no lit word defined! Unable to compile push");
        exit(1);
    }
    
    //printf("CPSH: %ld\n", val);

    *(cell*) en.mem->cur = (cell) w_lit;
    en.mem->cur += sizeof(cell);

    *(cell*) en.mem->cur = val;
    en.mem->cur += sizeof(cell);

    //*en.newword_sz += 2;
}
void
abort_compile(struct environ en) {
    *en.mode = MODE_INTERPRET;
    en.mem->cur = *en.newword;
    printf("Compilation of word %s aborted\n", *en.newword_name);
}
void
compile_token(struct environ en,
              struct token   tok) {
    int id = identify(tok.val);

    if (check_mem(en, tok)) {
        abort_compile(en);
        return;
    }

    switch (id) {
        case ID_INT:
            compile_push(tok, en, strtoll(tok.val, NULL, 10));
            break;
        case ID_HEX:
            compile_push(tok, en, strtoll(tok.val, NULL, 16));
            break;
        case ID_BIN:
            compile_push(tok, en, strtoll(tok.val, NULL, 2));
            break;
        case ID_CHAR:
            compile_push(tok, en, (cell) tok.val[1]);
            break;
        case ID_FLT:
            printf("Floats are not implemented yet\n");
            exit(1);
        case ID_WORD:
            compile_word(en, tok);
            break;
        default:
            printf("Token id: %d\n", id);
            assert(0 && "Unknown token type");
    }
}

void
handle_token(struct environ en,
             struct token   tok) {

    if (*en.mode == MODE_INTERPRET) {
        execute_token(en, tok);
        return;
    }

    if (*en.mode == MODE_COMPILE) {
        compile_token(en, tok);
        return;
    }
}

size_t
stack_cells(struct stack *st) {
    return (st->begin - st->cur)/sizeof(cell);
}

// Pushes the next cell onto stack, skips it
void
builtin_lit(struct environ en,
            struct token   tok) {

    push(en, tok, en.st, (cell) **en.ip);
    ++*en.ip;
}

// Pushes the string onto stack, skips it
void
builtin_strlit(struct environ en,
               struct token   tok) {

    push(en, tok, en.st, (cell) *en.ip);

    char *end = (char*)*en.ip;
    for (; *end; end++);

    //printf("STRLIT: %s\n", (char*)*en.ip);

    *en.ip = (cell*)(end + 1);
}

void
builtin_ret(struct environ en,
            struct token   tok) {


    cell new_ip = pop(en, tok, en.rst);

    //printf("RET to %p\n", (void*)new_ip);

    *en.ip = (cell*) new_ip;
}

void
builtin_colon(struct environ en,
              struct token   tok) {
    if (*en.mode == MODE_COMPILE) {
        error(tok, "Already in compilation mode");
        return;
    }

    struct token name = next(*en.inpt);

    if (name.val == NULL) {
        error(tok, "No name provided");
        return;
    }

    *en.newword_name = en.mem->cur;
    strcpy(en.mem->cur, name.val);
    en.mem->cur += strlen(name.val) + 1;

    *en.newword = en.mem->cur;

    //printf(":: %p\n", *en.newword);
    //*en.newword_sz = 0;

    *en.mode = MODE_COMPILE;

    //*en.xt = NULL;
}

// Pushes the current IP, sets the IP to the body
void
docol(struct environ en, struct token tok) {
    //printf("DOCOL %s\n", (*en.xt)->name);
    push(en, tok, en.rst, (cell) *en.ip);
    *en.ip = (cell*) (*en.xt)->body;
}

void
builtin_scolon(struct environ en,
               struct token   tok) {
    if (*en.mode != MODE_COMPILE) {
        error(tok, "Not in the compilation mode");
        return;
    }

    struct entry *ret = dict_search(*en.dict, "ret");
    if (ret == NULL) {
        error(tok, "CRITICAL ERROR: no ret word defined! Unable to finish off word declaration");
        exit(1);
    }

    *(cell*) en.mem->cur = (cell) ret;
    en.mem->cur += sizeof(cell);
    //++*en.newword_sz;

    //struct entry *prev = *en.dict;
    //printf("New word name: %s\n", *en.newword_name);
    *en.dict = dict_append(en.mem,
                           *en.dict,
                           *en.newword_name,
                           0,
                           //*en.newword_sz,
                           *en.newword,
                           docol);
    //printf("Prev word: %p, this word: %p\n", prev, *en.dict);

    *en.mode = MODE_INTERPRET;

}

void
builtin_immediate(struct environ en,
                  struct token   tok) {
    (void)tok;
    (*en.dict)->flags |= DICT_FLAG_IMMED;
}

void
builtin_componly(struct environ en,
                 struct token   tok) {
    (void)tok;
    (*en.dict)->flags |= DICT_FLAG_COMPONLY;
}

void
builtin_intronly(struct environ en,
                 struct token   tok) {
    (void)tok;
    (*en.dict)->flags |= DICT_FLAG_INTRONLY;
}

int
expect_stack(struct environ en,
             struct stack *st,
             struct token tok,
             size_t num) {
    if (stack_cells(st) < num) {
        char buf[128];
        snprintf(buf, 128, "Expected %lu cells on stack, got %lu", num, stack_cells(st));
        error(tok, buf);
        throw(en, tok, THROW_STACK_UNDERFLOW);
        return 1;
    }
    return 0;
}

void
builtin_into_r(struct environ en,
               struct token   tok) {
    if (expect_stack(en, en.st, tok, 1)) return;
    cell c = pop(en, tok, en.st);
    push(en, tok, en.rst, c);
}

void
builtin_from_r(struct environ en,
               struct token   tok) {
    if (expect_stack(en, en.rst, tok, 1)) return;
    cell c = pop(en, tok, en.rst);
    push(en, tok, en.st, c);
}

void
builtin_top_r(struct environ en,
              struct token   tok) {
    if (expect_stack(en, en.rst, tok, 1)) return;
    cell c = top(en, tok, en.rst);
    push(en, tok, en.st, c);
}

void
builtin_execute(struct environ en,
                struct token   tok) {
    if (expect_stack(en, en.st, tok, 1)) return;
    *en.xt = (struct entry*)pop(en, tok, en.st);

    (*en.xt)->code(en, tok);
}

void
builtin_branch(struct environ en,
               struct token   tok) {
    (void)tok;
    cell off = **en.ip;
    //*en.ip += off;
    *en.ip = (cell*) off;
}

void
builtin_0branch(struct environ en,
                struct token   tok) {
    if (expect_stack(en, en.st, tok, 1)) return;

    int cond = pop(en, tok, en.st);
    cell off = **en.ip;

    if (cond) {
        ++*en.ip;
        return;
    }

    //*en.ip += off;
    *en.ip = (cell*) off;
}

void
builtin_cells(struct environ en,
              struct token   tok) {
    if (expect_stack(en, en.st, tok, 1)) return;
    cell num = pop(en, tok, en.st);
    push(en, tok, en.st, (cell) (num * sizeof(cell)));
}

void
builtin_allot(struct environ en,
              struct token   tok) {
    if (expect_stack(en, en.st, tok, 1)) return;
    cell sz = pop(en, tok, en.st);
    en.mem->cur += sz;
    check_mem(en, tok);
    //printf("ALLOT: %ld\n", sz);
}

void
builtin_here(struct environ en,
             struct token   tok) {
    push(en, tok, en.st, (cell)en.mem->cur);
}

void
builtin_mode(struct environ en,
             struct token   tok) {
    push(en, tok, en.st, (cell)en.mode);
}

void
builtin_mem_begin(struct environ en,
                  struct token   tok) {
    push(en, tok, en.st, (cell)en.mem->mem);
}

void
builtin_mem_end(struct environ en,
                struct token   tok) {
    push(en, tok, en.st, (cell)(en.mem->mem + en.mem->sz));
}

void
builtin_at(struct environ en,
           struct token   tok) {
    if (expect_stack(en, en.st, tok, 1)) return;
    cell *addr = (cell*) pop(en, tok, en.st);
    push(en, tok, en.st, *addr);
}

void
builtin_bat(struct environ en,
           struct token   tok) {
    if (expect_stack(en, en.st, tok, 1)) return;
    byte *addr = (byte*) pop(en, tok, en.st);
    push(en, tok, en.st, (cell) *addr);
}

void
builtin_put(struct environ en,
            struct token   tok) {
    if (expect_stack(en, en.st, tok, 2)) return;

    cell *addr = (cell*) pop(en, tok, en.st);
    cell val = pop(en, tok, en.st);

    *addr = val;
    //printf("!: %ld -> %p\n", val, addr);
}

void
builtin_bput(struct environ en,
             struct token   tok) {
    if (expect_stack(en, en.st, tok, 2)) return;

    byte *addr = (byte*) pop(en, tok, en.st);
    byte val = (byte) pop(en, tok, en.st);

    *addr = val;
}

void
builtin_pad(struct environ en,
            struct token   tok) {
    push(en, tok, en.st, (cell) en.pad);
}

void
builtin_dict_search(struct environ en,
                    struct token   tok) {

    struct token name = next(*en.inpt);

    struct entry *e = dict_search(*en.dict, name.val);

    //printf("Tick: (%s) %p -> %p\n", e->name, e, e->next);

    push(en, tok, en.st, (cell) e);
}

void
builtin_dict(struct environ en,
            struct token   tok) {
    push(en, tok, en.st, (cell) *en.dict);
}

void
builtin_dict_next(struct environ en,
                  struct token   tok) {
    if (expect_stack(en, en.st, tok, 1)) return;
    struct entry *e = (struct entry*) pop(en, tok, en.st);
    push(en, tok, en.st, (cell) e->next);
}

void
builtin_dict_name(struct environ en,
                  struct token   tok) {
    if (expect_stack(en, en.st, tok, 1)) return;
    struct entry *e = (struct entry*) pop(en, tok, en.st);
    push(en, tok, en.st, (cell) e->name);
}

void
builtin_dict_flag(struct environ en,
                  struct token   tok) {
    if (expect_stack(en, en.st, tok, 1)) return;
    struct entry *e = (struct entry*) pop(en, tok, en.st);
    push(en, tok, en.st, (cell) e->flags);
}

void
builtin_dict_code(struct environ en,
                  struct token   tok) {
    if (expect_stack(en, en.st, tok, 1)) return;
    struct entry *e = (struct entry*) pop(en, tok, en.st);
    push(en, tok, en.st, (cell) e->code);
}

void
builtin_dict_body(struct environ en,
                  struct token   tok) {
    if (expect_stack(en, en.st, tok, 1)) return;
    struct entry *e = (struct entry*) pop(en, tok, en.st);
    push(en, tok, en.st, (cell) e->body);
}

//void
//builtin_dict_bdsz(struct environ en,
//                  struct token   tok) {
//    if (expect_stack(en, en.st, tok, 1)) return;
//    struct entry *e = (struct entry*) pop(en, tok, en.st);
//    push(en, tok, en.st, (cell) e->body_sz);
//}

void
builtin_drop(struct environ en,
             struct token   tok) {
    if (expect_stack(en, en.st, tok, 1)) return;
    (void)pop(en, tok, en.st);
}

void
builtin_dup(struct environ en,
            struct token   tok) {
    if (expect_stack(en, en.st, tok, 1)) return;
    cell c = top(en, tok, en.st);
    push(en, tok, en.st, c);
}

void
builtin_swap(struct environ en,
             struct token   tok) {
    if (expect_stack(en, en.st, tok, 2)) return;
    cell a = pop(en, tok, en.st);
    cell b = pop(en, tok, en.st);

    push(en, tok, en.st, a);
    push(en, tok, en.st, b);
}

void
builtin_over(struct environ en,
             struct token   tok) {
    if (expect_stack(en, en.st, tok, 2)) return;
    cell a = pop(en, tok, en.st);
    cell b = pop(en, tok, en.st);

    push(en, tok, en.st, b);
    push(en, tok, en.st, a);
    push(en, tok, en.st, b);
}

// a b c -- b c a
void
builtin_rot(struct environ en,
             struct token   tok) {
    if (expect_stack(en, en.st, tok, 2)) return;
    cell c = pop(en, tok, en.st);
    cell b = pop(en, tok, en.st);
    cell a = pop(en, tok, en.st);

    push(en, tok, en.st, b);
    push(en, tok, en.st, c);
    push(en, tok, en.st, a);
}

void
builtin_add(struct environ en,
            struct token   tok) {
    if (expect_stack(en, en.st, tok, 2)) return;
    cell a = pop(en, tok, en.st);
    cell b = pop(en, tok, en.st);

    push(en, tok, en.st, a + b);
}

void
builtin_mul(struct environ en,
            struct token   tok) {
    if (expect_stack(en, en.st, tok, 2)) return;
    cell a = pop(en, tok, en.st);
    cell b = pop(en, tok, en.st);

    push(en, tok, en.st, a * b);
}

void
builtin_div(struct environ en,
            struct token   tok) {
    if (expect_stack(en, en.st, tok, 2)) return;
    cell b = pop(en, tok, en.st);
    cell a = pop(en, tok, en.st);

    push(en, tok, en.st, a/b);
}

void
builtin_sub(struct environ en,
            struct token   tok) {
    if (expect_stack(en, en.st, tok, 2)) return;
    cell a = pop(en, tok, en.st);
    cell b = pop(en, tok, en.st);

    push(en, tok, en.st, b - a);
}

void
builtin_equ(struct environ en,
            struct token   tok) {
    if (expect_stack(en, en.st, tok, 2)) return;
    cell a = pop(en, tok, en.st);
    cell b = pop(en, tok, en.st);

    push(en, tok, en.st, a == b);
}

void
builtin_not(struct environ en,
            struct token   tok) {
    if (expect_stack(en, en.st, tok, 1)) return;
    cell c = pop(en, tok, en.st);

    push(en, tok, en.st, !c);
}

void
builtin_and(struct environ en,
            struct token   tok) {
    if (expect_stack(en, en.st, tok, 1)) return;
    cell a = pop(en, tok, en.st);
    cell b = pop(en, tok, en.st);

    push(en, tok, en.st, a && b);
}

void
builtin_or(struct environ en,
            struct token   tok) {
    if (expect_stack(en, en.st, tok, 1)) return;
    cell a = pop(en, tok, en.st);
    cell b = pop(en, tok, en.st);

    push(en, tok, en.st, a || b);
}

void
builtin_less(struct environ en,
             struct token   tok) {
    if (expect_stack(en, en.st, tok, 2)) return;
    cell a = pop(en, tok, en.st);
    cell b = pop(en, tok, en.st);

    push(en, tok, en.st, b < a);
}

void
builtin_gr(struct environ en,
           struct token   tok) {
    if (expect_stack(en, en.st, tok, 2)) return;
    cell a = pop(en, tok, en.st);
    cell b = pop(en, tok, en.st);

    push(en, tok, en.st, b > a);
}

void
builtin_source(struct environ en,
               struct token   tok) {
    push(en, tok, en.st, (cell) (*en.inpt)->source);
}

void
builtin_src_cur(struct environ en,
                 struct token   tok) {
    push(en, tok, en.st, (cell) (*en.inpt)->cur);
}

void
builtin_src2b(struct environ en,
              struct token   tok) {
    push(en, tok, en.st, *(*en.inpt)->cur);
    if (*(*en.inpt)->cur) advance(*en.inpt);
}

void
builtin_b2t(struct environ en,
            struct token   tok) {
    byte b = (byte)pop(en, tok, en.st);
    putchar(b);
}

void
builtin_cr(struct environ en,
           struct token   tok) {
    (void)en;
    (void)tok;
    putchar('\n');
}

void
builtin_dot(struct environ en,
            struct token   tok) {
    if (expect_stack(en, en.st, tok, 1)) return;
    cell c = pop(en, tok, en.st);
    printf("%ld", c);
}

int refill(struct input *st);
void
builtin_file_as_source(struct environ en,
                       struct token   tok) {
    if (expect_stack(en, en.st, tok, 1)) return;
    FILE *f = (FILE*)pop(en, tok, en.st);
    struct input new_in = {0};
    new_in.source_file = f;
    *en.inps = inputs_append(*en.inps, new_in);
    *en.inpt = inputs_top(*en.inps); 
    refill(*en.inpt);
}

void
builtin_open_file(struct environ en,
                  struct token   tok) {
    if (expect_stack(en, en.st, tok, 2)) return;
    cell method = pop(en, tok, en.st);
    char *filename = (char*)pop(en, tok, en.st);
    char *s_method = NULL;

    switch(method) {
        case 0:
            s_method = "rb";
            break;
        case 1:
            // Don't truncate files
            s_method = "ab";
            break;
        case 2:
            s_method = "rwb";
            break;
        default:
            push(en, tok, en.st, 0);
            push(en, tok, en.st, THROW_INVALID_ARGUMENT);
            return;

    }

    FILE *f = fopen(filename, s_method);

    if (f == NULL) {
        push(en, tok, en.st, 0);
        // This is not the only possible reason...
        push(en, tok, en.st, THROW_FILE_NONEXISTENT);
        return;
    }

    push(en, tok, en.st, (cell)f);
    push(en, tok, en.st, 0);
}

void
builtin_close_file(struct environ en,
                   struct token   tok) {
    if (expect_stack(en, en.st, tok, 1)) return;
    FILE *file = (FILE*)pop(en, tok, en.st);

    push(en, tok, en.st, !!fclose(file));
}

void
builtin_catch(struct environ en,
              struct token   tok) {
    (void) tok;
    *en.catch = *en.ip;
    *en.catch_st = en.st->cur;
    *en.catch_rst = en.rst->cur;

    push(en, tok, en.st, 0);
}

void
throw(struct environ en,
      struct token   tok,
      cell           val) {

    if (!val) return;

    cell *before = *en.ip;

    *en.ip = *en.catch;
    en.st->cur =  *en.catch_st;
    en.rst->cur = *en.catch_rst;

    // Toplevel catcher
    if (*en.ip == NULL) {
        printf("(%d:%d '%s') CAUGHT %ld from %p\n",
                tok.line+1, tok.col+1, tok.val, val, before);
        // Error wasn't caught, terminate source execution
        *en.terminate = 1;
        return;
    }

    push(en, tok, en.st, val);
}

void
builtin_throw(struct environ en,
              struct token   tok) {
    if (expect_stack(en, en.st, tok, 1)) return;
    cell val = pop(en, tok, en.st);

    throw(en, tok, val);
}

void
builtin_stackdump(struct environ en,
                  struct token   tok) {
    printf("(%s %d:%d) initiating stack dump\n", tok.val, tok.line, tok.col);
    printf("      DATA STACK\n");
    for (cell *p = (cell*) en.st->cur; p < (cell*) en.st->begin; p++) {
        if (*p < 128 && *p != '\n')
            printf("  %16ld ('%c') %11p\n", *p, (char)*p, (void*)*p);
        else
            printf("  %16ld %16p\n", *p, (void*)*p);
    }
    printf("      RETURN STACK\n");
    for (cell *p = (cell*) en.rst->cur; p < (cell*) en.rst->begin; p++) {
        printf("  %16ld %16p\n", *p, (void*)*p);
    }
    //raise(SIGINT);
    //exit(0);
}

void
builtin_worddump(struct environ en,
                 struct token   tok) {
    printf("(%s %d:%d) initiating word dump\n", tok.val, tok.line, tok.col);

    struct token tk = next(*en.inpt);
    if (tk.val == NULL || !strlen(tk.val)) {
        error(tok, "No argument provided for worddump!");
        raise(SIGINT);
        exit(1);
    }

    struct entry *e = dict_search(*en.dict, tk.val);

    if (e == NULL) {
        error(tok, "Word doesn't exist");
        raise(SIGINT);
        exit(1);
    }

    printf("Worddump for %s\n", e->name);
    for (struct entry **w = (struct entry**) e->body;
         (*w)->code != builtin_ret; w++) {

        printf("  %s ", (*w)->name);

        if ((*w)->code == builtin_lit) {
            printf("%ld\n", *(((cell*)w)+1));
            w++;
        }
        else if ((*w)->code == builtin_strlit) {
            char *str = (char *)(w + 1);
            printf("%s\n", str);

            char *end = str;
            for (; *end; end++);

            w = ((struct entry**) (end + 1)) - 1; 
        }
        else if ((*w)->code == builtin_branch ||
            (*w)->code == builtin_0branch) {

            cell *p = (cell*)*(((cell*)w)+1);

            printf("%p (%ld)\n", p, p - (cell*)w);
            w++;
        }
        else putchar('\n');
    }


    //raise(SIGINT);
    //exit(0);
}

void
eval(struct environ en) {

    struct token tok = next(*en.inpt);

    while (tok.val != NULL) {
        if (!strlen(tok.val)) {
            tok = next(*en.inpt);
            continue;
        }
        //printf("(%d:%d '%s')\n", tok.line + 1, tok.col + 1, tok.val);
        handle_token(en, tok);

        if (*en.terminate) break;

        tok = next(*en.inpt);
    }
}

//char *
//readfile(const char *filename) {
//    FILE *f = fopen(filename, "r");
//
//    if (f == NULL) {
//        perror("fopen()");
//        return NULL;
//    }
//
//    char *code = NULL;
//    size_t code_sz = 0;
//
//    while (!feof(f) && !ferror(f)) {
//        code_sz++;
//        code = realloc(code, code_sz);
//        code[code_sz - 1] = fgetc(f);
//    }
//    code[code_sz - 1] = 0;
//
//    fclose(f);
//
//    return code;
//}

int
refill(struct input *st) {
    if (feof(st->source_file)) return THROW_EOF;

    if (fgets(st->source, SOURCE_SIZE, st->source_file) == NULL)
        return THROW_EOF;

    st->cur = st->source;

    //printf("Refilled: %s\n", st->source);

    return 0;
}

void
builtin_refill(struct environ en,
               struct token   tok) {
    push(en, tok, en.st, refill(*en.inpt));
}

int
main(int argc, char **argv) {
    if (argc < 2) {
        printf("Please note that XT is basically non-functional without\n");
        printf("a file that lays out definitions of some basic words\n");
        printf("You can use basis.f, which should come together with XT: %s basis.f\n", argv[0]);
    }

    char *mem_buf = malloc(MEMORY_SIZE);
    struct memory mem = {
        mem_buf,
        mem_buf,
        MEMORY_SIZE
    };

    struct stack stk = new_stack(&mem, STACK_SIZE);
    struct stack rstk = new_stack(&mem, RSTACK_SIZE);

    struct entry *dict = NULL;

    char mode = MODE_INTERPRET;

    char *newword = NULL;
    char *newword_name = NULL;
    //size_t newword_sz = 0;
    cell *ip = NULL;
    cell *catch = NULL;
    char *catch_st = stk.cur;
    char *catch_rst = rstk.cur;
    struct entry *xt = NULL;
    char *pad = malloc(PAD_SIZE);
    char terminate = 0;

    // Hide these words somehow
    dict = dict_append_builtin(&mem, dict, "lit", 0, builtin_lit);
    dict = dict_append_builtin(&mem, dict, "strlit", 0, builtin_strlit);
    dict = dict_append_builtin(&mem, dict, "ret", 0, builtin_ret);

    dict = dict_append_builtin(&mem, dict, ">r", 0, builtin_into_r);
    dict = dict_append_builtin(&mem, dict, "r>", 0, builtin_from_r);
    dict = dict_append_builtin(&mem, dict, "@r", 0, builtin_top_r);

    dict = dict_append_builtin(&mem, dict, "execute", 0, builtin_execute);
    dict = dict_append_builtin(&mem, dict, "branch", DICT_FLAG_COMPONLY, builtin_branch);
    dict = dict_append_builtin(&mem, dict, "0branch", DICT_FLAG_COMPONLY, builtin_0branch);

    dict = dict_append_builtin(&mem, dict, "cells", 0, builtin_cells);
    dict = dict_append_builtin(&mem, dict, "allot", 0, builtin_allot);
    dict = dict_append_builtin(&mem, dict, "here", 0, builtin_here);
    dict = dict_append_builtin(&mem, dict, "mode", 0, builtin_mode);
    dict = dict_append_builtin(&mem, dict, "mem-begin", 0, builtin_mem_begin);
    dict = dict_append_builtin(&mem, dict, "mem-end", 0, builtin_mem_end);
    dict = dict_append_builtin(&mem, dict, "@", 0, builtin_at);
    dict = dict_append_builtin(&mem, dict, "!", 0, builtin_put);
    dict = dict_append_builtin(&mem, dict, "b@", 0, builtin_bat);
    dict = dict_append_builtin(&mem, dict, "b!", 0, builtin_bput);

    dict = dict_append_builtin(&mem, dict, "pad", 0, builtin_pad);
    dict = dict_append_builtin(&mem, dict, "'", 0, builtin_dict_search);
    dict = dict_append_builtin(&mem, dict, "dict", 0, builtin_dict);
    dict = dict_append_builtin(&mem, dict, "->next", 0, builtin_dict_next);
    dict = dict_append_builtin(&mem, dict, "->name", 0, builtin_dict_name);
    dict = dict_append_builtin(&mem, dict, "->flag", 0, builtin_dict_flag);
    dict = dict_append_builtin(&mem, dict, "->code", 0, builtin_dict_code);
    //dict = dict_append_builtin(&mem, dict, "->bdsz", 0, builtin_dict_bdsz);
    dict = dict_append_builtin(&mem, dict, "->body", 0, builtin_dict_body);

    dict = dict_append_builtin(&mem, dict, ":", 0, builtin_colon);
    dict = dict_append_builtin(&mem, dict, ";", DICT_FLAG_IMMED, builtin_scolon);

    dict = dict_append_builtin(&mem, dict, "immediate", 0, builtin_immediate);
    dict = dict_append_builtin(&mem, dict, "compile-only", 0, builtin_componly);
    dict = dict_append_builtin(&mem, dict, "interpret-only", 0, builtin_intronly);

    dict = dict_append_builtin(&mem, dict, "drop", 0, builtin_drop);
    dict = dict_append_builtin(&mem, dict, "swap", 0, builtin_swap);
    dict = dict_append_builtin(&mem, dict, "over", 0, builtin_over);
    dict = dict_append_builtin(&mem, dict, "rot", 0, builtin_rot);
    dict = dict_append_builtin(&mem, dict, "dup", 0, builtin_dup);

    dict = dict_append_builtin(&mem, dict, "+", 0, builtin_add);
    dict = dict_append_builtin(&mem, dict, "*", 0, builtin_mul);
    dict = dict_append_builtin(&mem, dict, "-", 0, builtin_sub);
    dict = dict_append_builtin(&mem, dict, "/", 0, builtin_div);
    dict = dict_append_builtin(&mem, dict, "=", 0, builtin_equ);
    dict = dict_append_builtin(&mem, dict, "not", 0, builtin_not);
    dict = dict_append_builtin(&mem, dict, "and", 0, builtin_and);
    dict = dict_append_builtin(&mem, dict, "or", 0, builtin_or);
    dict = dict_append_builtin(&mem, dict, "<", 0, builtin_less);
    dict = dict_append_builtin(&mem, dict, ">", 0, builtin_gr);

    dict = dict_append_builtin(&mem, dict, "refill", 0, builtin_refill);
    dict = dict_append_builtin(&mem, dict, "source", 0, builtin_source);
    dict = dict_append_builtin(&mem, dict, "&src", 0, builtin_src_cur);
    dict = dict_append_builtin(&mem, dict, "src>b", 0, builtin_src2b);
    dict = dict_append_builtin(&mem, dict, "b>t", 0, builtin_b2t);
    dict = dict_append_builtin(&mem, dict, "cr", 0, builtin_cr);
    dict = dict_append_builtin(&mem, dict, ".", 0, builtin_dot);

    dict = dict_append_builtin(&mem, dict, "file-as-source", 0, builtin_file_as_source);
    dict = dict_append_builtin(&mem, dict, "file-open", 0, builtin_open_file);
    //dict = dict_append_builtin(&mem, dict, "read-file", 0, builtin_refill);
    dict = dict_append_builtin(&mem, dict, "file-close", 0, builtin_close_file);

    dict = dict_append_builtin(&mem, dict, "catch", 0, builtin_catch);
    dict = dict_append_builtin(&mem, dict, "throw", 0, builtin_throw);

    dict = dict_append_builtin(&mem, dict, "stackdump", 0, builtin_stackdump);
    dict = dict_append_builtin(&mem, dict, "worddump", 0, builtin_worddump);

    struct environ en = {
         NULL,
         NULL,
         &mem,
         &stk,
         &rstk,
         &dict,
         &mode,
         pad,
         &newword_name,
         &newword,
         &ip,
         &xt,
         &catch,
         &catch_st,
         &catch_rst,
         &terminate
    };

    struct inputs *inps = malloc(sizeof(struct input));
    inps->num = 0;

    struct input term = {0};
    term.source_file = stdin;
    inps = inputs_append(inps, term);

    // Get the expected behaviour of files begin executed left-to-right
    for (int i = argc - 1; i > 0; i--) {
        FILE *file = fopen(argv[i], "r");
        if (file == NULL) {
            perror("fopen()");
            continue;
        }
        struct input in = {0};
        in.source_file = file;
        inps = inputs_append(inps, in);
    }

    en.inps = &inps;

    for (;;) {
        struct input *in = inputs_top(inps);
        en.inpt = &in;

        // End of all sources
        if (in == NULL) break;

        if (in->source_file == stdin)
            printf("> ");

        while (!refill(inputs_top(inps))) {
            eval(en);

            in = inputs_top(inps);

            if (terminate) {
                terminate = 0;
                break;
            }
            if (in->source_file == stdin)
                printf("> ");
        }

        // Input may have changed!
        in = inputs_top(inps);

        if (feof(in->source_file) || ferror(in->source_file)) {
            fclose(in->source_file);
            inps = inputs_pop(inps);
        }
    }

    free(inps);

    free(mem.mem);
    free(pad);
}
