#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "builtins.h"
#include "input.h"
#include "defs.h"

extern struct inputs    *inps;
extern struct input     *inpt;
extern struct memory     mem;
extern struct stack      st;
extern struct stack      rst;
extern struct entry     *dict;
extern byte              mode;
extern byte             *pad;

/* Used for compilation mode */
extern char             *newword_name;
extern byte             *newword;

/* Inner interpreter */
extern cell             *ip;
extern struct entry     *xt; 
extern cell             *catch;
extern byte             *catch_st;
extern byte             *catch_rst;
extern byte              terminate;

void throw(struct token, cell);

void error(struct token t, const char *msg);

// NOTE: This is not a classical include file,
// this will be included into the xt.c file only
//
// Pushes the next cell onto stack, skips it

size_t
stack_cells(struct stack *st) {
    return (st->begin - st->cur)/sizeof(cell);
}

void
builtin_lit(struct token tok) {

    push(tok, &st, (cell) *ip);
    ++ip;
}

// Pushes the string onto stack, skips it
void
builtin_strlit(struct token tok) {

    push(tok, &st, (cell) ip);

    byte *end = (byte*)ip;
    for (; *end; end++);

    //printf("STRLIT: %s\n", (byte*)ip);

    ip = (cell*)(end + 1);
}

void
builtin_ret(struct token tok) {


    cell new_ip = pop(tok, &rst);

    //printf("RET to %p\n", (void*)new_ip);

    ip = (cell*) new_ip;
}

void
builtin_colon(struct token tok) {
    if (mode == MODE_COMPILE) {
        error(tok, "Already in compilation mode");
        return;
    }

    struct token name = next(inpt);

    if (name.val == NULL) {
        error(tok, "No name provided");
        return;
    }

    newword_name = (char*)mem.cur;
    strcpy((char*)mem.cur, name.val);
    mem.cur += strlen(name.val) + 1;

    newword = mem.cur;

    //printf(":: %p\n", newword);
    //newword_sz = 0;

    mode = MODE_COMPILE;

    //xt = NULL;
}

// Pushes the current IP, sets the IP to the body
void
docol(struct token tok) {
    //printf("DOCOL %s\n", xt->name);
    push(tok, &rst, (cell) ip);
    ip = (cell*) xt->body;
}

void
builtin_scolon(struct token tok) {
    if (mode != MODE_COMPILE) {
        error(tok, "Not in the compilation mode");
        return;
    }

    struct entry *ret = dict_search(dict, "ret");
    if (ret == NULL) {
        error(tok, "CRITICAL ERROR: no ret word defined! Unable to finish off word declaration");
        exit(1);
    }

    *(cell*) mem.cur = (cell) ret;
    mem.cur += sizeof(cell);
    //++newword_sz;

    //struct entry *prev = dict;
    //printf("New word name: %s\n", newword_name);
    dict = dict_append(&mem,
                           dict,
                           newword_name,
                           0,
                           //newword_sz,
                           newword,
                           docol);
    //printf("Prev word: %p, this word: %p\n", prev, dict);

    mode = MODE_INTERPRET;

}

void
builtin_immediate(struct token tok) {
    (void)tok;
    (dict)->flags |= DICT_FLAG_IMMED;
}

void
builtin_componly(struct token tok) {
    (void)tok;
    (dict)->flags |= DICT_FLAG_COMPONLY;
}

void
builtin_intronly(struct token tok) {
    (void)tok;
    (dict)->flags |= DICT_FLAG_INTRONLY;
}

int
expect_stack(struct stack *st,
             struct token tok,
             size_t num) {
    if (stack_cells(st) < num) {
        char buf[128];
        snprintf(buf, 128, "Expected %lu cells on stack, got %lu", num, stack_cells(st));
        error(tok, buf);
        throw(tok, THROW_STACK_UNDERFLOW);
        return 1;
    }
    return 0;
}

void
builtin_into_r(struct token tok) {
    if (expect_stack(&st, tok, 1)) return;
    cell c = pop(tok, &st);
    push(tok, &rst, c);
}

void
builtin_from_r(struct token tok) {
    if (expect_stack(&rst, tok, 1)) return;
    cell c = pop(tok, &rst);
    push(tok, &st, c);
}

void
builtin_top_r(struct token tok) {
    if (expect_stack(&rst, tok, 1)) return;
    cell c = top(tok, &rst);
    push(tok, &st, c);
}

void
builtin_execute(struct token tok) {
    if (expect_stack(&st, tok, 1)) return;
    xt = (struct entry*)pop(tok, &st);

    xt->code(tok);
}

void
builtin_branch(struct token tok) {
    (void)tok;
    cell off = *ip;
    ip = (cell*) off;
}

void
builtin_0branch(struct token tok) {
    if (expect_stack(&st, tok, 1)) return;

    int cond = pop(tok, &st);
    cell off = *ip;

    if (cond) {
        ++ip;
        return;
    }

    ip = (cell*) off;
}

void
builtin_cells(struct token tok) {
    if (expect_stack(&st, tok, 1)) return;
    cell num = pop(tok, &st);
    push(tok, &st, (cell) (num * sizeof(cell)));
}

void
builtin_allot(struct token tok) {
    if (expect_stack(&st, tok, 1)) return;
    cell sz = pop(tok, &st);
    mem.cur += sz;
    check_mem(tok);
    //printf("ALLOT: %ld\n", sz);
}

void
builtin_here(struct token tok) {
    push(tok, &st, (cell)mem.cur);
}

void
builtin_mode(struct token tok) {
    push(tok, &st, (cell)&mode);
}

void
builtin_mem_begin(struct token tok) {
    push(tok, &st, (cell)mem.mem);
}

void
builtin_mem_end(struct token tok) {
    push(tok, &st, (cell)(mem.mem + mem.sz));
}

void
builtin_at(struct token tok) {
    if (expect_stack(&st, tok, 1)) return;
    cell *addr = (cell*) pop(tok, &st);
    push(tok, &st, *addr);
}

void
builtin_bat(struct token tok) {
    if (expect_stack(&st, tok, 1)) return;
    byte *addr = (byte*) pop(tok, &st);
    push(tok, &st, (cell) *addr);
}

void
builtin_put(struct token tok) {
    if (expect_stack(&st, tok, 2)) return;

    cell *addr = (cell*) pop(tok, &st);
    cell val = pop(tok, &st);

    *addr = val;
    //printf("!: %ld -> %p\n", val, addr);
}

void
builtin_bput(struct token tok) {
    if (expect_stack(&st, tok, 2)) return;

    byte *addr = (byte*) pop(tok, &st);
    byte val = (byte) pop(tok, &st);

    //printf("BPUT %d:%d\n", tok.line, tok.col);
    //printf("BPUT: %d -> %p\n", val, addr);

    *addr = val;
}

// Allocate heap memory block
void
builtin_allocate(struct token tok) {
    if (expect_stack(&st, tok, 1)) return;
    cell amount = pop(tok, &st);
    void *p = malloc(amount);

    push(tok, &st, (cell) p);

    push(tok, &st,
            p == NULL ? THROW_MALLOC_FAIL : 0);
}

// Resize heap memory block
void
builtin_resize(struct token tok) {
    if (expect_stack(&st, tok, 2)) return;
    cell size = pop(tok, &st);
    void *p = (void*) pop(tok, &st);

    p = realloc(p, size);

    push(tok, &st, (cell) p);

    push(tok, &st,
            p == NULL ? THROW_REALLOC_FAIL : 0);
}

// Free heap block
void
builtin_free(struct token tok) {
    if (expect_stack(&st, tok, 1)) return;
    void *p = (void*) pop(tok, &st);

    free(p);

    // This is a placeholder
    // Standard free() does not allow safe recovery
    // We may need to implement some sort of wrappers
    push(tok, &st, 0);
}

// Install (any sort of) a memory block as a new memory
void
builtin_mem_install(struct token tok) {
    if (expect_stack(&st, tok, 2)) return;
    cell size = pop(tok, &st);
    byte *p = (byte*) pop(tok, &st);

    // Too small
    if (size < sizeof(struct memory)) {
        push(tok, &st, THROW_MEM_INSTALL_FAIL);
        return;
    }

    // Add metadata about the previous memory
    //*(struct memory*)p = mem;

    mem.mem = p;
    mem.cur = p /*+ sizeof(struct memory)*/;
    mem.sz = size;

    push(tok, &st, 0);
}

// Pop the topmost installed memory block
void
builtin_mem_pop(struct token tok) {
    struct memory metadata = *(struct memory*)mem.mem;
    if (metadata.sz == 0) {
        push(tok, &st, THROW_MEM_POP_FAIL);
        return;
    }

    struct entry *dict = dict;
    // While dictionary entries are within the currently installed memory segment
    while (((byte *)dict > mem.mem)
         &&((byte *)dict < (mem.mem + mem.sz))) {
        if (dict == NULL) {
            error(tok, "INTERNAL CRITICAL ERROR: No dictionary entry within new bounds were found!");
            exit(1);
        }
        dict = dict->next;
    }

    dict = dict;

    mem = metadata;

    push(tok, &st, 0);
}

void
builtin_pad(struct token tok) {
    push(tok, &st, (cell) pad);
}

void
builtin_dict_search(struct token tok) {

    struct token name = next(inpt);

    struct entry *e = dict_search(dict, name.val);

    //printf("Tick: (%s) %p -> %p\n", e->name, e, e->next);

    push(tok, &st, (cell) e);

    push(tok, &st, (e == NULL) ? THROW_UNDEFINED_WORD : 0);
}

void
builtin_dict(struct token tok) {
    push(tok, &st, (cell) dict);
}

void
builtin_dict_next(struct token tok) {
    if (expect_stack(&st, tok, 1)) return;
    struct entry *e = (struct entry*) pop(tok, &st);
    push(tok, &st, (cell) e->next);
}

void
builtin_dict_name(struct token tok) {
    if (expect_stack(&st, tok, 1)) return;
    struct entry *e = (struct entry*) pop(tok, &st);
    push(tok, &st, (cell) e->name);
}

void
builtin_dict_flag(struct token tok) {
    if (expect_stack(&st, tok, 1)) return;
    struct entry *e = (struct entry*) pop(tok, &st);
    push(tok, &st, (cell) e->flags);
}

void
builtin_dict_code(struct token tok) {
    if (expect_stack(&st, tok, 1)) return;
    struct entry *e = (struct entry*) pop(tok, &st);
    push(tok, &st, (cell) e->code);
}

void
builtin_dict_body(struct token tok) {
    if (expect_stack(&st, tok, 1)) return;
    struct entry *e = (struct entry*) pop(tok, &st);
    push(tok, &st, (cell) e->body);
}

void
builtin_drop(struct token tok) {
    if (expect_stack(&st, tok, 1)) return;
    (void)pop(tok, &st);
}

void
builtin_dup(struct token tok) {
    if (expect_stack(&st, tok, 1)) return;
    cell c = top(tok, &st);
    push(tok, &st, c);
}

void
builtin_swap(struct token tok) {
    if (expect_stack(&st, tok, 2)) return;
    cell a = pop(tok, &st);
    cell b = pop(tok, &st);

    push(tok, &st, a);
    push(tok, &st, b);
}

void
builtin_over(struct token tok) {
    if (expect_stack(&st, tok, 2)) return;
    cell a = pop(tok, &st);
    cell b = pop(tok, &st);

    push(tok, &st, b);
    push(tok, &st, a);
    push(tok, &st, b);
}

// a b c -- b c a
void
builtin_rot(struct token tok) {
    if (expect_stack(&st, tok, 2)) return;
    cell c = pop(tok, &st);
    cell b = pop(tok, &st);
    cell a = pop(tok, &st);

    push(tok, &st, b);
    push(tok, &st, c);
    push(tok, &st, a);
}

void
builtin_add(struct token tok) {
    if (expect_stack(&st, tok, 2)) return;
    cell a = pop(tok, &st);
    cell b = pop(tok, &st);

    push(tok, &st, a + b);
}

void
builtin_mul(struct token tok) {
    if (expect_stack(&st, tok, 2)) return;
    cell a = pop(tok, &st);
    cell b = pop(tok, &st);

    push(tok, &st, a * b);
}

void
builtin_div(struct token tok) {
    if (expect_stack(&st, tok, 2)) return;
    cell b = pop(tok, &st);
    cell a = pop(tok, &st);

    push(tok, &st, a/b);
}

void
builtin_sub(struct token tok) {
    if (expect_stack(&st, tok, 2)) return;
    cell a = pop(tok, &st);
    cell b = pop(tok, &st);

    push(tok, &st, b - a);
}

void
builtin_equ(struct token tok) {
    if (expect_stack(&st, tok, 2)) return;
    cell a = pop(tok, &st);
    cell b = pop(tok, &st);

    push(tok, &st, a == b ? -1 : 0);
}

void
builtin_not(struct token tok) {
    if (expect_stack(&st, tok, 1)) return;
    cell c = pop(tok, &st);

    push(tok, &st, ~c);
}

void
builtin_and(struct token tok) {
    if (expect_stack(&st, tok, 1)) return;
    cell a = pop(tok, &st);
    cell b = pop(tok, &st);

    push(tok, &st, a & b);
}

void
builtin_or(struct token tok) {
    if (expect_stack(&st, tok, 1)) return;
    cell a = pop(tok, &st);
    cell b = pop(tok, &st);

    push(tok, &st, a | b);
}

void
builtin_less(struct token tok) {
    if (expect_stack(&st, tok, 2)) return;
    cell a = pop(tok, &st);
    cell b = pop(tok, &st);

    push(tok, &st, b < a ? -1 : 0);
}

void
builtin_gr(struct token tok) {
    if (expect_stack(&st, tok, 2)) return;
    cell a = pop(tok, &st);
    cell b = pop(tok, &st);

    push(tok, &st, b > a ? -1 : 0);
}

void
builtin_source(struct token tok) {
    push(tok, &st, (cell) inpt->source);
}

void
builtin_src_cur(struct token tok) {
    push(tok, &st, (cell) inpt->cur);
}

void
builtin_src2b(struct token tok) {
    push(tok, &st, *(inpt)->cur);
    if (*(inpt)->cur) advance(inpt);
}

void
builtin_b2t(struct token tok) {
    byte b = (byte)pop(tok, &st);
    write(STDOUT_FILENO, &b, 1);
}

void
builtin_cr(struct token tok) {
    (void)tok;
    write(STDOUT_FILENO, "\n", 1);
}

void
builtin_dot(struct token tok) {
    if (expect_stack(&st, tok, 1)) return;
    cell c = pop(tok, &st);
    char buf[256];
    snprintf(buf, 256, "%ld", c);
    write(STDOUT_FILENO, buf, strlen(buf));
}

int refill(struct input *st);
int
check_fd_readable(int fd) {
    struct stat st;
    if (fstat(fd, &st)) return 0;
    // There's nothing like S_ISREADABLE...
    // Maybe somehow check otherwise?

    return 1;
}
void
builtin_file_as_source(struct token tok) {
    if (expect_stack(&st, tok, 1)) return;
    int f = (int)pop(tok, &st);

    if (!check_fd_readable(f)) {
        push(tok, &st, THROW_IO_ERR);
        return;
    }

    struct input new_in = {0};
    new_in.source_file = f;
    inps = inputs_append(inps, new_in);
    inpt = inputs_top(inps); 
    refill(inpt);

    push(tok, &st, 0);
}

int
mode_by_method(int m) {
    //printf("Method: %d\n", m);
    switch(m) {
        // r/o
        case 0:
            return O_RDONLY;
        // w/o
        case 1:
            // Don't truncate files
            return O_WRONLY | O_APPEND | O_CREAT;
        // r/w
        case 2:
            return O_RDWR | O_APPEND | O_CREAT;
    }
    return -1;
}

void
builtin_open_file(struct token tok) {
    if (expect_stack(&st, tok, 2)) return;
    cell method = pop(tok, &st);
    char *filename = (char*)pop(tok, &st);
    int mode = mode_by_method(method);
    
    if (mode == -1) {
        push(tok, &st, (cell)-1);
        push(tok, &st, THROW_INVALID_ARGUMENT);
        return;
    }

    int f = open(filename, mode);

    if (f < 0) {
        push(tok, &st, -1);
        // This is not the only possible reason...
        push(tok, &st, THROW_FILE_NONEXISTENT);
        return;
    }

    push(tok, &st, (cell)f);
    push(tok, &st, 0);
}

void
builtin_file_create(struct token tok) {
    if (expect_stack(&st, tok, 2)) return;
    cell method =  pop(tok, &st);
    char *filename = (char*)pop(tok, &st);
    int mode = mode_by_method(method);

    if (mode == 0) {
        push(tok, &st, (cell)-1);
        push(tok, &st, THROW_INVALID_ARGUMENT);
        return;
    }

    int f = open(filename, mode | O_CREAT | O_TRUNC,
                    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    if (f < 0) {
        perror("open()");
        push(tok, &st, -1);
        // Which error code to throw?
        push(tok, &st, THROW_IO_ERR);
        return;
    }

    push(tok, &st, (cell)f);
    push(tok, &st, 0);
}

void
builtin_close_file(struct token tok) {
    if (expect_stack(&st, tok, 1)) return;
    int file = (int)pop(tok, &st);

    push(tok, &st, !!close(file));
}

void
builtin_file_size(struct token tok) {
    if(expect_stack(&st, tok, 1)) return;
    int fd = (int)pop(tok, &st);

    off_t off = lseek(fd, 0, SEEK_CUR);

    if (off < 0) {
        push(tok, &st, 0);
        push(tok, &st, THROW_IO_ERR);
        return;
    }

    if (lseek(fd, 0, SEEK_SET)) {
        push(tok, &st, 0);
        push(tok, &st, THROW_IO_ERR);
        return;
    }

    off_t end = lseek(fd, 0, SEEK_END);

    if (end < 0) {
        push(tok, &st, 0);
        push(tok, &st, THROW_IO_ERR);
        return;
    }

    if (lseek(fd, off, SEEK_SET)) {
        push(tok, &st, 0);
        push(tok, &st, THROW_IO_ERR);
        return;
    }

    push(tok, &st, (cell)end);
    push(tok, &st, 0);
}


// ( ptr sz fd -- num ior)
void
builtin_file_read(struct token tok) {
    if(expect_stack(&st, tok, 3)) return;
    int fd = (int)pop(tok, &st);
    cell sz = (cell)pop(tok, &st);
    byte *ptr = (byte*)pop(tok, &st);

    ssize_t got = read(fd, ptr, sz);
    push(tok, &st, (cell)got);

    if (got >= 0)
        push(tok, &st, 0);
    else
        push(tok, &st, THROW_IO_ERR);
}

// ( ptr sz fd -- num ior)
void
builtin_file_write(struct token tok) {
    if(expect_stack(&st, tok, 3)) return;
    int fd = (int)pop(tok, &st);
    cell sz = (cell)pop(tok, &st);
    byte *ptr = (byte*)pop(tok, &st);

    ssize_t got = write(fd, ptr, sz);
    push(tok, &st, (cell)got);

    if (got >= 0)
        push(tok, &st, 0);
    else
        push(tok, &st, THROW_IO_ERR);
}

void
builtin_catch(struct token tok) {
    (void) tok;
    catch = ip;
    catch_st = st.cur;
    catch_rst = rst.cur;

    push(tok, &st, 0);
}

void
builtin_throw(struct token tok) {
    if (expect_stack(&st, tok, 1)) return;
    cell val = pop(tok, &st);

    throw(tok, val);
}

// Helper code word
// Push the value stored in the body pointer
void
crpush(struct token tok) {
    push(tok, &st, (cell) xt->body);
}

void
builtin_create(struct token tok) {
    struct token name = next(inpt);

    if (name.val == NULL) {
        error(tok, "No name provided");
        return;
    }

    newword_name = (char*)mem.cur;
    strcpy((char*)mem.cur, name.val);
    mem.cur += strlen(name.val) + 1;

    dict = dict_append(&mem,
                           dict,
                           newword_name,
                           0,
                           NULL,
                           crpush);

    // Set the data field pointer
    dict->body = mem.cur;
}

// Helper code word for does>
// Push the first cell of the body
// Set IP to the value of the second cell of the body
void
pushjump(struct token tok) {
    //printf("PUSHJUMP: %ld, %p\n",
    //        (cell) *(cell*) xt->body,
    //        (cell*) *(((cell*) xt->body) + 1));
    push(tok, &st, *(cell*) xt->body);

    push(tok, &rst, (cell) ip);
    ip = (cell*) *(((cell*) xt->body) + 1);
}

void
builtin_does(struct token tok) {

    struct entry *word = dict;

    // Compile helper word
    // Helper word:
    // Push data pointer
    // Execute the code that follows does>
    byte *helperbody = (byte*) mem.cur;
    // Push
    *(cell*) mem.cur = (cell) word->body;
    mem.cur += sizeof(cell);
    // Jump
    *(cell*) mem.cur = (cell) ip;
    mem.cur += sizeof(cell);

    // Modify the CREATEd word
    word->code = pushjump;
    word->body = helperbody;

    // Return
    cell new_ip = pop(tok, &rst);
    ip = (cell*) new_ip;
}

void
builtin_stackdump(struct token tok) {
    printf("(%s %d:%d) initiating stack dump\n", tok.val, tok.line, tok.col);
    printf("      DATA STACK\n");
    for (cell *p = (cell*) st.cur; p < (cell*) st.begin; p++) {
        if (*p < 128 && *p != '\n')
            printf("  %16ld ('%c') %11p\n", *p, (char)*p, (void*)*p);
        else
            printf("  %16ld %16p\n", *p, (void*)*p);
    }
    printf("      RETURN STACK\n");
    for (cell *p = (cell*) rst.cur; p < (cell*) rst.begin; p++) {
        printf("  %16ld %16p\n", *p, (void*)*p);
    }
    fflush(stdout);
    //raise(SIGINT);
    //exit(0);
}

void
builtin_worddump(struct token tok) {
    printf("(%s %d:%d) initiating word dump\n", tok.val, tok.line, tok.col);

    struct token tk = next(inpt);
    if (tk.val == NULL || !strlen(tk.val)) {
        error(tok, "No argument provided for worddump!");
        //raise(SIGINT);
        exit(1);
    }

    struct entry *e = dict_search(dict, tk.val);

    if (e == NULL) {
        error(tok, "Word doesn't exist");
        //raise(SIGINT);
        exit(1);
    }

    printf("Worddump for '%s'\n", e->name);
    if (e->code != docol) {
        printf("<assembly>\n");
        return;
    }
    for (struct entry **w = (struct entry**) e->body;
         (*w)->code != builtin_ret; w++) {

        printf("  %s ", (*w)->name);

        if ((*w)->code == builtin_lit) {
            printf("%ld\n", *(((cell*)w)+1));
            w++;
        }
        else if ((*w)->code == builtin_strlit) {
            byte *str = (byte *)(w + 1);
            printf("%s\n", str);

            byte *end = str;
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

    fflush(stdout);
    //raise(SIGINT);
    //exit(0);
}

void
builtin_refill(struct token tok) {
    push(tok, &st, refill(inpt));
}

void
builtin_quit(struct token tok) {
    (void)tok;
    free(inps);
    free(mem.mem);
    free(pad);
    exit(0);
}

void
init_dict(void) {
//Dictionary initialization {{{
// This is huge and ugly, but c'est la vie. Using some sort of a loop would
// require introducing additional data structures and redundant complexity,
// which makes no sense, since this is only one function.
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

    dict = dict_append_builtin(&mem, dict, "heap-allocate", 0, builtin_allocate);
    dict = dict_append_builtin(&mem, dict, "heap-resize", 0, builtin_resize);
    dict = dict_append_builtin(&mem, dict, "heap-free", 0, builtin_free);
    dict = dict_append_builtin(&mem, dict, "mem-install", 0, builtin_mem_install);
    //dict = dict_append_builtin(&mem, dict, "mem-pop", 0, builtin_mem_pop);

    dict = dict_append_builtin(&mem, dict, "pad", 0, builtin_pad);
    dict = dict_append_builtin(&mem, dict, "'", 0, builtin_dict_search);
    dict = dict_append_builtin(&mem, dict, "dict", 0, builtin_dict);
    dict = dict_append_builtin(&mem, dict, "->next", 0, builtin_dict_next);
    dict = dict_append_builtin(&mem, dict, "->name", 0, builtin_dict_name);
    dict = dict_append_builtin(&mem, dict, "->flag", 0, builtin_dict_flag);
    dict = dict_append_builtin(&mem, dict, "->code", 0, builtin_dict_code);
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
    dict = dict_append_builtin(&mem, dict, "*source", 0, builtin_src_cur);
    dict = dict_append_builtin(&mem, dict, "source>", 0, builtin_src2b);
    dict = dict_append_builtin(&mem, dict, ">term", 0, builtin_b2t);
    //dict = dict_append_builtin(&mem, dict, "cr", 0, builtin_cr);
    dict = dict_append_builtin(&mem, dict, ".", 0, builtin_dot);

    dict = dict_append_builtin(&mem, dict, "file-open", 0, builtin_open_file);
    dict = dict_append_builtin(&mem, dict, "file-create", 0, builtin_file_create);
    dict = dict_append_builtin(&mem, dict, "file-close", 0, builtin_close_file);
    dict = dict_append_builtin(&mem, dict, "file-read", 0, builtin_file_read);
    dict = dict_append_builtin(&mem, dict, "file-write", 0, builtin_file_write);
    dict = dict_append_builtin(&mem, dict, "file-size", 0, builtin_file_size);
    dict = dict_append_builtin(&mem, dict, "file-as-source", 0, builtin_file_as_source);

    dict = dict_append_builtin(&mem, dict, "catch", 0, builtin_catch);
    dict = dict_append_builtin(&mem, dict, "throw", 0, builtin_throw);

    dict = dict_append_builtin(&mem, dict, "create", 0, builtin_create);
    dict = dict_append_builtin(&mem, dict, "does>", 0/*DICT_FLAG_COMPONLY*/, builtin_does);

    dict = dict_append_builtin(&mem, dict, "stackdump", 0, builtin_stackdump);
    dict = dict_append_builtin(&mem, dict, "worddump", 0, builtin_worddump);

    dict = dict_append_builtin(&mem, dict, "quit", 0, builtin_quit);
//}}}
}

