#include <stdint.h>

#include "token.h"
#include "dict.h"
#include "stack.h"
#include "lexer.h"

void init_dict(void);
// Pushes the next cell onto stack, skips it
size_t stack_cells(struct stack *st);

void builtin_lit(struct token tok);

// Pushes the string onto stack, skips it
void builtin_strlit(struct token tok);

void builtin_ret(struct token tok);

void builtin_colon(struct token tok);

// Pushes the current IP, sets the IP to the body
void docol(struct token tok);

void builtin_scolon(struct token tok);

void builtin_immediate(struct token tok);

void builtin_componly(struct token tok);

void builtin_intronly(struct token tok);

int expect_stack(struct stack *st, struct token tok, size_t num);

void builtin_into_r(struct token tok);

void builtin_from_r(struct token tok);

void builtin_top_r(struct token tok);
void builtin_execute(struct token tok);
void builtin_branch(struct token tok);
void builtin_0branch(struct token tok);
void builtin_cells(struct token tok);
void builtin_allot(struct token tok);
void builtin_here(struct token tok);
void builtin_mode(struct token tok);
void builtin_mem_begin(struct token tok);
void builtin_mem_end(struct token tok);
void builtin_at(struct token tok);
void builtin_bat(struct token tok);
void builtin_put(struct token tok);
void builtin_bput(struct token tok);
// Allocate heap memory block
void builtin_allocate(struct token tok);
// Resize heap memory block
void builtin_resize(struct token tok);
// Free heap block
void builtin_free(struct token tok);
// Install (any sort of) a memory block as a new memory
void builtin_mem_install(struct token tok);
// Pop the topmost installed memory block
void builtin_mem_pop(struct token tok);
void builtin_pad(struct token tok);
void builtin_dict_search(struct token tok);
void builtin_dict(struct token tok);
void builtin_dict_next(struct token tok);
void builtin_dict_name(struct token tok);
void builtin_dict_flag(struct token tok);
void builtin_dict_code(struct token tok);
void builtin_dict_body(struct token tok);
void builtin_drop(struct token tok);
void builtin_dup(struct token tok);
void builtin_swap(struct token tok);
void builtin_over(struct token tok);
// a b c -- b c a
void builtin_rot(struct token tok);
void builtin_add(struct token tok);
void builtin_mul(struct token tok);
void builtin_div(struct token tok);
void builtin_sub(struct token tok);
void builtin_equ(struct token tok);
void builtin_not(struct token tok);

void builtin_and(struct token tok);
void builtin_or(struct token tok);
void builtin_less(struct token tok);
void builtin_gr(struct token tok);
void builtin_source(struct token tok);
void builtin_src_cur(struct token tok);
void builtin_src2b(struct token tok);
void builtin_b2t(struct token tok);
void builtin_cr(struct token tok);
void builtin_dot(struct token tok);
int check_fd_readable(int fd);
void builtin_file_as_source(struct token tok);
int mode_by_method(int m);
void builtin_open_file(struct token tok);
void builtin_file_create(struct token tok);
void builtin_close_file(struct token tok);
void builtin_file_size(struct token tok);

// ( ptr sz fd -- num ior)
void builtin_file_read(struct token tok);
// ( ptr sz fd -- num ior)
void builtin_file_write(struct token tok);
void builtin_catch(struct token tok);

void builtin_throw(struct token tok);
// Helper code word
// Push the value stored in the body pointer
void crpush(struct token tok);
void builtin_create(struct token tok);
// Helper code word for does>
// Push the first cell of the body
// Set IP to the value of the second cell of the body
void pushjump(struct token tok);
void builtin_does(struct token tok);
void builtin_stackdump(struct token tok);
void builtin_worddump(struct token tok);
void builtin_refill(struct token tok);
void builtin_quit(struct token tok);
