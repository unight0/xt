#ifndef LEXER_H
#define LEXER_H

#include "input.h"
#include "token.h"

#define ID_WORD            0
#define ID_INT             1
#define ID_HEX             2
#define ID_BIN             3
#define ID_FLT             4
#define ID_CHAR            5

struct token next(struct input *st);
void         advance(struct input *st);
int          identify(char *tok);

#endif
