#ifndef PARSER_H
#define PARSER_H

#include "token.h"

#define MAX_ARGS 128
#define MAX_COMMANDS 16

typedef struct {
    char *argv[MAX_ARGS];      /* strdup'd copies of token text */
    int argc;
    char input[MAX_TOKEN_LEN]; /* "" if none */
    char output[MAX_TOKEN_LEN];
    int append;                /* 1 for >>, 0 for > */
    int background;            /* 1 if & */
} command_t;

typedef struct {
    command_t commands[MAX_COMMANDS];
    int command_count;
} pipeline_t;

void command_init(command_t *cmd);
int parse(token_list_t *tokens, pipeline_t *pipeline);   /* 1 ok, 0 error */
void pipeline_print(const pipeline_t *pipeline);

#endif /* PARSER_H */
