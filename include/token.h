#ifndef TOKEN_H
#define TOKEN_H

#include <stddef.h>

#define MAX_TOKENS 128
#define MAX_TOKEN_LEN 128

typedef enum {
    TOKEN_WORD,
    TOKEN_PIPE,
    TOKEN_INPUT,
    TOKEN_OUTPUT,
    TOKEN_APPEND,
    TOKEN_BACKGROUND,
    TOKEN_END
} token_type_t;

typedef struct {
    token_type_t type;
    char text[MAX_TOKEN_LEN];
} token_t;

typedef struct {
    token_t tokens[MAX_TOKENS];
    int count;
} token_list_t;

void token_list_init(token_list_t *list);
void token_add(token_list_t *list, token_type_t type, const char *text);
const char* token_name(token_type_t type);
void token_print(const token_list_t *list);

#endif /* TOKEN_H */