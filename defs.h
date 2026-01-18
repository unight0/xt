#ifndef DEFS_H
#define DEFS_H

#include <stdint.h>
#include <stdlib.h>

#define THROW_STACK_OVERFLOW   -3
#define THROW_STACK_UNDERFLOW  -4
//#define THROW_RSTACK_OVERFLOW  -5
//#define THROW_RSTACK_UNDERFLOW -6
#define THROW_UNDEFINED_WORD   -13
#define THROW_COMPONLY_WORD    -14
#define THROW_IO_ERR           -37
#define THROW_EOF              -39
#define THROW_MEM_POP_FAIL     -991
#define THROW_MEM_INSTALL_FAIL -992
#define THROW_MALLOC_FAIL      -993
#define THROW_REALLOC_FAIL     -994
// This is a placeholder, free()
// cannot fail safely
#define THROW_FREE_FAIL        -995
#define THROW_FILE_NONEXISTENT -996
#define THROW_INVALID_ARGUMENT -997
#define THROW_INTRONLY_WORD    -998
#define THROW_OUT_OF_MEM       -999

#define PAD_SIZE           1024
#define MODE_INTERPRET     0
#define MODE_COMPILE       1


typedef uint64_t cell;
typedef uint8_t  byte;

#endif
