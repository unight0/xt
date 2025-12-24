#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>

#define DICT_FLAG_IMMED    1
#define DICT_FLAG_COMPONLY 1 << 1
#define DICT_FLAG_INTRONLY 1 << 2
#define MEMORY_SIZE        32*1024
#define STACK_SIZE         256
#define RSTACK_SIZE        512
#define MODE_INTERPRET     0
#define MODE_COMPILE       1
#define ID_WORD            0
#define ID_INT             1
#define ID_HEX             2
#define ID_BIN             3
#define ID_FLT             4


typedef uint64_t cell;
typedef uint8_t  byte;

struct tstate {
    int   line;
    int   col;
    char *cur;
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

struct environ {
    struct tstate *term;
    struct memory *mem;
    struct stack  *st;
    struct stack  *rst;
    struct entry  **dict;
    char          *mode;
    /* Used for compilation mode */
    char          **newword_name;
    char          **newword;
    //size_t        *newword_sz;
    /* Inner interpreter */
    cell         **ip;
    struct entry **xt; 
};

void
advance(struct tstate *st) {
    //assert(*st->cur && "Cannot advance past the end of string");
    st->col++;
    if (*st->cur == '\n') {
        st->col = 0;
        st->line++;
    }
    st->cur++;
}

struct token
next(struct tstate *st) {
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
    for (size_t i = 0; i < 1000000; /*i++*/) {
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
push(struct token t, struct stack *st, cell c) {
    if (st->cur - sizeof(cell) <= st->end)
        error(t, "Stack overflow!");
    st->cur -= sizeof(cell);
    *(cell*) st->cur = c;
}

cell
top(struct token t, struct stack *st) {
    if (st->cur == st->begin)
        error(t, "Stack underflow!");
    cell c = *(cell*) st->cur;
    return c;
}

cell
pop(struct token t, struct stack *st) {
    cell c = top(t, st);
    st->cur += sizeof(cell);
    return c;
}
int
identify(const char *tok) {
    assert(strlen(tok) && "Empty token");

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
        error(tok, "Memory overflow!");
        return 1;
    }
    return 0;
}

void
NEXT(struct environ en,
     struct token   tok) {
    while (*en.ip != NULL) {
        *en.xt = (struct entry*)**en.ip;

        printf("NEXT(): %s\n", (*en.xt)->name);

        if ((*en.xt)->flags & DICT_FLAG_INTRONLY) {
            error(tok, "INTERNAL CRITICAL ERROR: Encountered an interpret-only word during NEXT()");
            exit(1);
        }

        // It is ESSENTIAL that IP is incremented BEFORE code
        // section is called. This cost me 2+ hours of debugging!
        ++*en.ip;

        (*en.xt)->code(en, tok);
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
}

void
execute_word_tok(struct environ en,
                 struct token tok) {
    struct entry *def = dict_search(*en.dict, tok.val);
    if (def == NULL) {
        error(tok, "Unknown word");
        return;
    }
    if (def->flags & DICT_FLAG_COMPONLY) {
        error(tok, "Word is compile-only");
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
            push(tok, en.st, strtoll(tok.val, NULL, 10));
            break;
        case ID_HEX:
            push(tok, en.st, strtoll(tok.val, NULL, 16));
            break;
        case ID_BIN:
            push(tok, en.st, strtoll(tok.val, NULL, 2));
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
    printf("CW: %s\n", e->name);
    if (e == NULL) {
        error(tok, "Unknown word");
        return;
    }
    if (e->flags & DICT_FLAG_IMMED) {
        printf("IMMED %s\n", e->name);
        execute_word(en, tok, e);
        return;
    }
    if (e->flags & DICT_FLAG_INTRONLY) {
        error(tok, "Word is interpret-only");
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
    
    printf("CPSH: %ld\n", val);

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

    push(tok, en.st, (cell) **en.ip);
    ++*en.ip;
}

void
builtin_ret(struct environ en,
            struct token   tok) {

    printf("RET\n");

    cell new_ip = pop(tok, en.rst);

    *en.ip = (cell*) new_ip;
}

void
builtin_colon(struct environ en,
              struct token   tok) {
    if (*en.mode == MODE_COMPILE) {
        error(tok, "Already in compilation mode");
        return;
    }

    struct token name = next(en.term);

    if (name.val == NULL) {
        error(tok, "No name provided");
        return;
    }

    *en.newword_name = en.mem->cur;
    strcpy(en.mem->cur, name.val);
    en.mem->cur += strlen(name.val) + 1;

    *en.newword = en.mem->cur;

    printf(":: %p\n", *en.newword);
    //*en.newword_sz = 0;

    *en.mode = MODE_COMPILE;

    //*en.xt = NULL;
}

// Pushes the current IP, sets the IP to the body
void
docol(struct environ en, struct token tok) {
    printf("DOCOL %s\n", (*en.xt)->name);
    push(tok, en.rst, (cell) *en.ip);
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

    struct entry *prev = *en.dict;
    printf("New word name: %s\n", *en.newword_name);
    *en.dict = dict_append(en.mem,
                           *en.dict,
                           *en.newword_name,
                           0,
                           //*en.newword_sz,
                           *en.newword,
                           docol);
    printf("Prev word: %p, this word: %p\n", prev, *en.dict);

    *en.mode = MODE_INTERPRET;

}

void
builtin_immediate(struct environ en,
                  struct token   tok) {
    (*en.dict)->flags |= DICT_FLAG_IMMED;
}

void
builtin_componly(struct environ en,
                 struct token   tok) {
    (*en.dict)->flags |= DICT_FLAG_COMPONLY;
}

void
builtin_intronly(struct environ en,
                 struct token   tok) {
    (*en.dict)->flags |= DICT_FLAG_INTRONLY;
}

int
expect_stack(struct stack *st, struct token tok, size_t num) {
    if (stack_cells(st) < num) {
        char buf[128];
        snprintf(buf, 128, "Expected %lu cells on stack, got %lu", num, stack_cells(st));
        error(tok, buf);
        return 1;
    }
    return 0;
}

void
builtin_into_r(struct environ en,
               struct token   tok) {
    if (expect_stack(en.st, tok, 1)) return;
    cell c = pop(tok, en.st);
    push(tok, en.rst, c);
}

void
builtin_from_r(struct environ en,
               struct token   tok) {
    if (expect_stack(en.rst, tok, 1)) return;
    cell c = pop(tok, en.rst);
    push(tok, en.st, c);
}

void
builtin_top_r(struct environ en,
              struct token   tok) {
    if (expect_stack(en.rst, tok, 1)) return;
    cell c = top(tok, en.rst);
    push(tok, en.st, c);
}

void
builtin_execute(struct environ en,
                struct token   tok) {
    if (expect_stack(en.st, tok, 1)) return;
    *en.xt = (struct entry*)pop(tok, en.st);

    (*en.xt)->code(en, tok);
}

void
builtin_branch(struct environ en,
               struct token   tok) {
    cell off = **en.ip;
    //*en.ip += off;
    *en.ip = (cell*) off;
}

void
builtin_0branch(struct environ en,
                struct token   tok) {
    if (expect_stack(en.st, tok, 1)) return;

    int cond = pop(tok, en.st);
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
    if (expect_stack(en.st, tok, 1)) return;
    cell num = pop(tok, en.st);
    push(tok, en.st, (cell) (num * sizeof(cell)));
}

void
builtin_allot(struct environ en,
              struct token   tok) {
    if (expect_stack(en.st, tok, 1)) return;
    cell sz = pop(tok, en.st);
    en.mem->cur += sz;
    printf("ALLOT: %ld\n", sz);
}

void
builtin_here(struct environ en,
             struct token   tok) {
    push(tok, en.st, (cell)en.mem->cur);
}

void
builtin_mode(struct environ en,
             struct token   tok) {
    push(tok, en.st, (cell)en.mode);
}

void
builtin_mem_begin(struct environ en,
                  struct token   tok) {
    push(tok, en.st, (cell)en.mem->mem);
}

void
builtin_mem_end(struct environ en,
                struct token   tok) {
    push(tok, en.st, (cell)(en.mem->mem + en.mem->sz));
}

void
builtin_at(struct environ en,
           struct token   tok) {
    if (expect_stack(en.st, tok, 1)) return;
    cell *addr = (cell*) pop(tok, en.st);
    push(tok, en.st, *addr);
}

void
builtin_bat(struct environ en,
           struct token   tok) {
    if (expect_stack(en.st, tok, 1)) return;
    byte *addr = (byte*) pop(tok, en.st);
    push(tok, en.st, (cell) *addr);
}

void
builtin_put(struct environ en,
            struct token   tok) {
    if (expect_stack(en.st, tok, 2)) return;

    cell *addr = (cell*) pop(tok, en.st);
    cell val = pop(tok, en.st);

    *addr = val;
    printf("!: %ld -> %p\n", val, addr);
}

void
builtin_bput(struct environ en,
             struct token   tok) {
    if (expect_stack(en.st, tok, 2)) return;

    byte *addr = (byte*) pop(tok, en.st);
    byte val = (byte) pop(tok, en.st);

    *addr = val;
}

void
builtin_dict_search(struct environ en,
                    struct token   tok) {

    struct token name = next(en.term);

    struct entry *e = dict_search(*en.dict, name.val);

    printf("Tick: (%s) %p -> %p\n", e->name, e, e->next);

    push(tok, en.st, (cell) e);
}

void
builtin_dict_next(struct environ en,
                  struct token   tok) {
    if (expect_stack(en.st, tok, 1)) return;
    struct entry *e = (struct entry*) pop(tok, en.st);
    push(tok, en.st, (cell) e->next);
}

void
builtin_dict_name(struct environ en,
                  struct token   tok) {
    if (expect_stack(en.st, tok, 1)) return;
    struct entry *e = (struct entry*) pop(tok, en.st);
    push(tok, en.st, (cell) e->name);
}

void
builtin_dict_flag(struct environ en,
                  struct token   tok) {
    if (expect_stack(en.st, tok, 1)) return;
    struct entry *e = (struct entry*) pop(tok, en.st);
    push(tok, en.st, (cell) e->flags);
}

void
builtin_dict_code(struct environ en,
                  struct token   tok) {
    if (expect_stack(en.st, tok, 1)) return;
    struct entry *e = (struct entry*) pop(tok, en.st);
    push(tok, en.st, (cell) e->code);
}

void
builtin_dict_body(struct environ en,
                  struct token   tok) {
    if (expect_stack(en.st, tok, 1)) return;
    struct entry *e = (struct entry*) pop(tok, en.st);
    push(tok, en.st, (cell) e->body);
}

//void
//builtin_dict_bdsz(struct environ en,
//                  struct token   tok) {
//    if (expect_stack(en.st, tok, 1)) return;
//    struct entry *e = (struct entry*) pop(tok, en.st);
//    push(tok, en.st, (cell) e->body_sz);
//}

void
builtin_drop(struct environ en,
             struct token   tok) {
    if (expect_stack(en.st, tok, 1)) return;
    (void)pop(tok, en.st);
}

void
builtin_dup(struct environ en,
            struct token   tok) {
    if (expect_stack(en.st, tok, 1)) return;
    cell c = top(tok, en.st);
    push(tok, en.st, c);
}

void
builtin_swap(struct environ en,
             struct token   tok) {
    if (expect_stack(en.st, tok, 2)) return;
    cell a = pop(tok, en.st);
    cell b = pop(tok, en.st);

    push(tok, en.st, a);
    push(tok, en.st, b);
}

void
builtin_over(struct environ en,
             struct token   tok) {
    if (expect_stack(en.st, tok, 2)) return;
    cell a = pop(tok, en.st);
    cell b = pop(tok, en.st);

    push(tok, en.st, b);
    push(tok, en.st, a);
    push(tok, en.st, b);
}

void
builtin_add(struct environ en,
            struct token   tok) {
    if (expect_stack(en.st, tok, 2)) return;
    cell a = pop(tok, en.st);
    cell b = pop(tok, en.st);

    push(tok, en.st, a + b);
}

void
builtin_sub(struct environ en,
            struct token   tok) {
    if (expect_stack(en.st, tok, 2)) return;
    cell a = pop(tok, en.st);
    cell b = pop(tok, en.st);

    push(tok, en.st, b - a);
}

void
builtin_equ(struct environ en,
            struct token   tok) {
    if (expect_stack(en.st, tok, 2)) return;
    cell a = pop(tok, en.st);
    cell b = pop(tok, en.st);

    push(tok, en.st, a == b);
}

void
builtin_less(struct environ en,
             struct token   tok) {
    if (expect_stack(en.st, tok, 2)) return;
    cell a = pop(tok, en.st);
    cell b = pop(tok, en.st);

    push(tok, en.st, b < a);
}

void
builtin_gr(struct environ en,
           struct token   tok) {
    if (expect_stack(en.st, tok, 2)) return;
    cell a = pop(tok, en.st);
    cell b = pop(tok, en.st);

    push(tok, en.st, b > a);
}

void
builtin_cr(struct environ en,
           struct token   tok) {
    if (expect_stack(en.st, tok, 0)) return;
    putchar('\n');
}

void
builtin_dot(struct environ en,
            struct token   tok) {
    if (expect_stack(en.st, tok, 1)) return;
    printf("%ld", pop(tok, en.st));
}

#include <signal.h>
void
builtin_stackdump(struct environ en,
                struct token   tok) {
    printf("(%s %d:%d) initiating stack dump\n", tok.val, tok.line, tok.col);
    printf("      DATA STACK\n");
    for (cell *p = (cell*) en.st->cur; p < (cell*) en.st->begin; p++) {
        printf("  %16ld %16p\n", *p, (void*)*p);
    }
    printf("      RETURN STACK\n");
    for (cell *p = (cell*) en.st->cur; p < (cell*) en.st->begin; p++) {
        printf("  %16ld %16p\n", *p, (void*)*p);
    }
    raise(SIGINT);
    exit(0);
}

void
builtin_worddump(struct environ en,
                 struct token   tok) {
    printf("(%s %d:%d) initiating word dump\n", tok.val, tok.line, tok.col);

    struct token tk = next(en.term);
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
    printf("At %p\n", e->body);
    for (struct entry **w = (struct entry**) e->body;
         (*w)->code != builtin_ret; w++) {

        printf("  %s\n", (*w)->name);

        if ((*w)->code == builtin_lit) {
            printf("  -> %ld\n", *(((cell*)w)+1));
            w++;
        }
        else if ((*w)->code == builtin_branch ||
            (*w)->code == builtin_0branch) {

            cell *p = (cell*)*(((cell*)w)+1);

            printf("  -> %p (%ld)\n", p, p - (cell*)w);
            w++;
        }
    }


    raise(SIGINT);
    exit(0);
}

int
main(void) {
    const char *code = 
        ": , here ! 1 cells allot ;\n"
        ": [ 0 mode b! ; immediate componly\n"
        ": ] 1 mode b! ; intronly\n"
        ": 'lit [ ' lit dup , , ] ;\n"
        ": lit, 'lit , , ;\n"
        ": ['] ' lit, ; immediate componly\n"
        ": postpone ' lit, ['] , , ; immediate componly\n"
        ": if postpone 0branch here 0 , ; immediate componly\n"
        ": else postpone branch here 1 cells + swap ! here 0 , ; immediate componly\n"
        ": then here swap ! ; immediate componly\n"
        // (limit index --)
        ": (do) >r >r ;\n"
        // (R:index limit -- done?)
        ": (loop) r> r@ swap 1 + dup >r > ;\n"
        ": do postpone (do) here ; immediate componly\n"
        ": loop postpone (loop) compile 0branch , ; immediate componly\n"
        ": hello 10 i do 111 . cr loop ; hello\n";

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
    struct entry *xt = NULL;

    char *code_copy = malloc(strlen(code) + 1);

    strcpy(code_copy, code);

    struct tstate st = {
        0, 0, code_copy
    };

    dict = dict_append_builtin(&mem, dict, "lit", 0, builtin_lit);
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

    dict = dict_append_builtin(&mem, dict, "'", 0, builtin_dict_search);
    dict = dict_append_builtin(&mem, dict, "->next", 0, builtin_dict_next);
    dict = dict_append_builtin(&mem, dict, "->name", 0, builtin_dict_name);
    dict = dict_append_builtin(&mem, dict, "->flag", 0, builtin_dict_flag);
    dict = dict_append_builtin(&mem, dict, "->code", 0, builtin_dict_code);
    //dict = dict_append_builtin(&mem, dict, "->bdsz", 0, builtin_dict_bdsz);
    dict = dict_append_builtin(&mem, dict, "->body", 0, builtin_dict_body);

    dict = dict_append_builtin(&mem, dict, ":", 0, builtin_colon);
    dict = dict_append_builtin(&mem, dict, ";", DICT_FLAG_IMMED, builtin_scolon);

    dict = dict_append_builtin(&mem, dict, "immediate", 0, builtin_immediate);
    dict = dict_append_builtin(&mem, dict, "componly", 0, builtin_componly);
    dict = dict_append_builtin(&mem, dict, "intronly", 0, builtin_intronly);

    dict = dict_append_builtin(&mem, dict, "drop", 0, builtin_drop);
    dict = dict_append_builtin(&mem, dict, "swap", 0, builtin_swap);
    dict = dict_append_builtin(&mem, dict, "over", 0, builtin_over);
    dict = dict_append_builtin(&mem, dict, "dup", 0, builtin_dup);

    dict = dict_append_builtin(&mem, dict, "+", 0, builtin_add);
    dict = dict_append_builtin(&mem, dict, "-", 0, builtin_sub);
    dict = dict_append_builtin(&mem, dict, "=", 0, builtin_equ);
    dict = dict_append_builtin(&mem, dict, "<", 0, builtin_less);
    dict = dict_append_builtin(&mem, dict, ">", 0, builtin_gr);

    dict = dict_append_builtin(&mem, dict, "cr", 0, builtin_cr);
    dict = dict_append_builtin(&mem, dict, ".", 0, builtin_dot);

    dict = dict_append_builtin(&mem, dict, "stackdump", 0, builtin_stackdump);
    dict = dict_append_builtin(&mem, dict, "worddump", 0, builtin_worddump);


    struct token tok = next(&st);

    while (tok.val != NULL) {
        if (!strlen(tok.val)) {
            tok = next(&st);
            continue;
        }
        //printf("(%d:%d '%s')\n", tok.line + 1, tok.col + 1, tok.val);
        handle_token((struct environ) {
                &st, 
                &mem,
                &stk,
                &rstk,
                &dict,
                &mode,
                &newword_name,
                &newword,
                //&newword_sz,
                &ip,
                &xt
        }, tok);

        tok = next(&st);
    }
}
