#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

//TODO?: change our execution mode from Indirect-Threaded
//Code to Subroutine-Threaded Code. (JIT compilation)
//That means:
//* Word bodies do not consist of dictionary entry pointers
//  anymore.
//* Word bodies are actual machine code
//* Shouldn't be too hard -- it is mostly CALL instructions
//* The main memory buffer needs to be executable (through mmap)
//
//+ We do not need NEXT, we just call the code region directly
//+ No need to allocate a return stack, we'll use the system one
//+ Performance (!)
//
//- Harder to compile
//- We become architecture-dependent (although easy to port)

#include "defs.h"
#include "input.h"
#include "stack.h"
#include "token.h"
#include "memory.h"
#include "dict.h"
#include "lexer.h"

// Many global variables. This is the best approach in this scenario. Otherwise
// we would have to pack all of this info into one big struct that would be
// constantly passed around functions. This creates not only a lot of visual
// noise, but also decreases performance. Moreover, global variables also map
// more directly to the purpose of the program: it is an interpreter, and these
// variables are exactly the entirety of its state. It makes no sense to have
// several interpretation instances within one XT instance.
/* General */
struct inputs    *inps;
struct input     *inpt;
struct memory     mem;
struct stack      st;
struct stack      rst;
struct entry     *dict;
byte              mode;
byte             *pad;

/* Used for compilation mode */
char             *newword_name;
byte             *newword;

/* Inner interpreter */
cell             *ip;
struct entry     *xt; 
cell             *catch;
byte             *catch_st;
byte             *catch_rst;
byte              terminate;

void throw(struct token, cell);

void
error(struct token t, const char *msg) {
    printf("Error at (%d:%d '%s'): %s\n", t.line + 1, t.col + 1, t.val, msg);
}

int
check_mem(struct token tok) {
    if (mem.cur > mem.mem + mem.sz) {
        error(tok, "Out of memory!");
        mem.cur = mem.mem + mem.sz;
        throw(tok, THROW_OUT_OF_MEM);
        return 1;
    }
    return 0;
}

void
NEXT(struct token tok) {
    while (ip != NULL) {
        xt = (struct entry*)*ip;

        //printf("NEXT(): %s\n", (xt)->name);

        if ((xt)->flags & DICT_FLAG_INTRONLY) {
            error(tok, "INTERNAL CRITICAL ERROR: Encountered an interpret-only word during NEXT()");
            exit(1);
        }

        // It is ESSENTIAL that IP is incremented BEFORE code
        // section is called. This cost me 2+ hours of debugging!
        ++ip;

        xt->code(tok);

        // Sometimes ip is advanced when we THROW
        // I don't want to bother ensuring that it is
        // always NULL
        // This assumes NULL == 0
        if (ip < (cell*)(3 * sizeof(cell))) break;
    }
}

// Entry into inner interpreter
void
execute_word(struct token tok,
             struct entry  *e) {

    xt = e;
    e->code(tok);
    NEXT(tok);

    // Catches set within the return stack cannot be returned to
    catch = NULL;
    catch_st = st.cur;
    catch_rst = rst.cur;
}

void
execute_word_tok(struct token tok) {
    struct entry *def = dict_search(dict, tok.val);
    if (def == NULL) {
        error(tok, "Unknown word");
        throw(tok, THROW_UNDEFINED_WORD);
        return;
    }
    if (def->flags & DICT_FLAG_COMPONLY) {
        error(tok, "Word is compile-only");
        throw(tok, THROW_COMPONLY_WORD);
        return;
    }
    execute_word(tok, def);
}
void
execute_token(struct token tok) {

    int id = identify(tok.val);

    switch (id) {
        case ID_INT:
            push(tok, &st, strtoll(tok.val, NULL, 10));
            break;
        case ID_HEX:
            push(tok, &st, strtoll(tok.val, NULL, 16));
            break;
        case ID_BIN:
            push(tok, &st, strtoll(tok.val, NULL, 2));
            break;
        case ID_CHAR:
            push(tok, &st, (cell) tok.val[1]);
            break;
        case ID_FLT:
            printf("Floats are not implemented yet\n");
            exit(1);
        case ID_WORD:
            execute_word_tok(tok);
            break;
        default:
            printf("Token id: %d\n", id);
            assert(0 && "Unknown token type");
    }
}

void
compile_word(struct token tok) {
    struct entry *e = dict_search(dict, tok.val);
    if (e == NULL) {
        error(tok, "Unknown word");
        throw(tok, THROW_UNDEFINED_WORD);
        return;
    }
    //printf("CW: %s\n", e->name);
    if (e->flags & DICT_FLAG_IMMED) {
        //printf("IMMED %s\n", e->name);
        execute_word(tok, e);
        return;
    }
    if (e->flags & DICT_FLAG_INTRONLY) {
        error(tok, "Word is interpret-only");
        throw(tok, THROW_INTRONLY_WORD);
        return;
    }
    *(cell*) mem.cur = (cell) e;
    mem.cur += sizeof(cell);
}

void
compile_push(struct token tok,
             cell           val) {

    struct entry *w_lit = dict_search(dict, "lit");
    
    if (w_lit == NULL) {
        error(tok, "CRITICAL ERROR: no lit word defined! Unable to compile push");
        exit(1);
    }
    
    //printf("CPSH: %ld\n", val);

    *(cell*) mem.cur = (cell) w_lit;
    mem.cur += sizeof(cell);

    *(cell*) mem.cur = val;
    mem.cur += sizeof(cell);
}
void
abort_compile() {
    mode = MODE_INTERPRET;
    mem.cur = newword;
    printf("Compilation of word %s aborted\n", newword_name);
}
void
compile_token(struct token tok) {
    int id = identify(tok.val);

    if (check_mem(tok)) {
        abort_compile();
        return;
    }

    switch (id) {
        case ID_INT:
            compile_push(tok, strtoll(tok.val, NULL, 10));
            break;
        case ID_HEX:
            compile_push(tok, strtoll(tok.val, NULL, 16));
            break;
        case ID_BIN:
            compile_push(tok, strtoll(tok.val, NULL, 2));
            break;
        case ID_CHAR:
            compile_push(tok, (cell) tok.val[1]);
            break;
        case ID_FLT:
            printf("Floats are not implemented yet\n");
            exit(1);
        case ID_WORD:
            compile_word(tok);
            break;
        default:
            printf("Token id: %d\n", id);
            assert(0 && "Unknown token type");
    }
}

void
handle_token(struct token tok) {

    if (mode == MODE_INTERPRET) {
        execute_token(tok);
        return;
    }

    if (mode == MODE_COMPILE) {
        compile_token(tok);
        return;
    }
}

const char*
throwstr(cell code) {
    switch (code) {
    case THROW_STACK_OVERFLOW:
    return "Stack overflow";
    case THROW_STACK_UNDERFLOW:
    return "Stack underflow";
    case THROW_UNDEFINED_WORD:
    return "Undefined word";
    case THROW_COMPONLY_WORD:
    return "Compile-only word";
    case THROW_IO_ERR:
    return "I/O error";
    case THROW_EOF:
    return "EOF";
    case THROW_MEM_POP_FAIL:
    return "Memory pop fail";
    case THROW_MEM_INSTALL_FAIL:
    return "Memory installation fail";
    case THROW_MALLOC_FAIL:
    return "Heap allocation fail";
    case THROW_REALLOC_FAIL:
    return "Heap reallocation fail";
    case THROW_FREE_FAIL:
    return "Heap free fail";
    case THROW_FILE_NONEXISTENT:
    return "File doesn't exist";
    case THROW_INVALID_ARGUMENT:
    return "Invalid argument";
    case THROW_INTRONLY_WORD:
    return "Interpret-only word";
    case THROW_OUT_OF_MEM:
    return "Out of memory";
    }
    return NULL;
}

void
throw(struct token tok,
      cell           val) {

    if (!val) return;

    cell *before = ip;

    ip = catch;
    st.cur = catch_st;
    rst.cur = catch_rst;

    // Toplevel catcher
    if (ip == NULL) {
        printf("(%d:%d '%s') CAUGHT %ld from %p\n",
                tok.line+1, tok.col+1, tok.val, val, before);
        const char *explain = throwstr(val);
        if (explain != NULL) {
            printf("%s\n", explain);
        }
        fflush(stdout);
        // Error wasn't caught, terminate source execution
        terminate = 1;
        return;
    }

    push(tok, &st, val);
}

#include "builtins.h"

void
eval() {
    struct token tok = next(inpt);

    while (tok.val != NULL) {
        if (!strlen(tok.val)) {
            tok = next(inpt);
            continue;
        }
        //printf("(%d:%d '%s')\n", tok.line + 1, tok.col + 1, tok.val);
        handle_token(tok);

        if (terminate) break;

        tok = next(inpt);
    }
}

//byte *
//readfile(const byte *filename) {
//    FILE *f = fopen(filename, "r");
//
//    if (f == NULL) {
//        perror("fopen()");
//        return NULL;
//    }
//
//    byte *code = NULL;
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
    size_t i = 0;

    //TODO: why are we reading byte-by-byte?
    for (; i < SOURCE_SIZE - 1; i++) {
        char c;
        int got = read(st->source_file, &c, 1);
        // End of file reached
        if (got == 0) {
            close(st->source_file);
            return THROW_EOF;
        }
        // Some I/O error happened
        if (got < 0) {
            close(st->source_file);
            return THROW_IO_ERR;
        }
        st->source[i] = c;
        if (c == '\n') break;
    }

    st->source[i+1] = 0;

    st->cur = st->source;

    return 0;
}

int
main(int argc, char **argv) {
    if (argc < 2) {
        printf("Please note that XT is basically non-functional without\n");
        printf("a file that lays out definitions of some basic words\n");
        printf("You can use basis.f, which should come together with XT: %s basis.f\n", argv[0]);
    }

    byte *mem_buf = malloc(MEMORY_SIZE);

    if (mem_buf == NULL) {
        perror("malloc()");
        exit(1);
    }

    // Add metadata about the previous memory (non-existent)
    *(struct memory*)mem_buf = (struct memory) {
        NULL, NULL, 0
    };

    mem = (struct memory) {
        mem_buf,
        mem_buf + sizeof(struct memory),
        MEMORY_SIZE
    };

    st = new_stack(&mem, STACK_SIZE);
    rst = new_stack(&mem, RSTACK_SIZE);

    mode = MODE_INTERPRET;

    pad = malloc(PAD_SIZE);

    if (pad == NULL) {
        perror("malloc()");
        exit(1);
    }

    dict = NULL;

    init_dict();

    inps = malloc(sizeof(struct input));
    if (inps == NULL) {
        perror("malloc()");
        exit(1);
    }

    inps->num = 0;

    struct input term = {0};
    term.source_file = STDIN_FILENO;
    inps = inputs_append(inps, term);

    // Get the expected behaviour of files begin executed left-to-right
    for (int i = argc - 1; i > 0; i--) {
        int file = open(argv[i], O_RDONLY);
        if (file < 0) {
            perror("open()");
            continue;
        }
        struct input in = {0};
        in.source_file = file;
        inps = inputs_append(inps, in);
    }

    // We use write() instead of printf() here to avoid
    // buffering issues, since the I/O builtins operate on
    // the fd-level, while printf() is a FILE* abstraction
    for (;;) {
        struct input *in = inputs_top(inps);
        inpt = in;

        // End of all sources
        if (in == NULL) break;

        if (in->source_file == STDIN_FILENO)
            write(STDOUT_FILENO, "> ", 2);

        while (!refill(inputs_top(inps))) {
            eval();

            in = inputs_top(inps);

            if (terminate) {
                terminate = 0;
                if (mode == MODE_COMPILE) {
                    // Exit compilation mode
                    mode = MODE_INTERPRET;
                    // Reset memory to pre-compilation state
                    mem.mem = newword;
                }
                if (in->source_file != STDIN_FILENO) break;
                continue;
            }
            if (in->source_file == STDIN_FILENO)
                write(STDOUT_FILENO, "> ", 2);
        }

        inps = inputs_pop(inps);
    }

    free(inps);
    free(mem.mem);
    free(pad);
}
