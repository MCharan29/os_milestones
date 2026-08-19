#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "token.h"
#include "lexer.h"
#include "history.h"
#include "parser.h"
#include "expand.h"

int main(void)
{
    printf("=====================================\n");
    printf("      Shellforge\n");
    printf(" A Unix Style Shell written in C\n");
    printf("=====================================\n");

    char *line;
    token_list_t tokens;
    pipeline_t pipeline;

    while (1)
    {
        line = readline("shellforge$ ");
        if (line == NULL)
        {
            printf("\nGoodbye!\n");
            break;
        }

        if (strlen(line) == 0)
        {
            free(line);
            continue;
        }

        add_history(line);
        history_add(line);

        if (lexer(line, &tokens) == 0)
        {
            token_print(&tokens);

            if (parse(&tokens, &pipeline))
            {
                expand_variables(&pipeline);
                pipeline_print(&pipeline);
            }
        }

        if (strcmp(line, "exit") == 0)
        {
            free(line);
            printf("Exiting...\n");
            break;
        }

        if (strcmp(line, "history") == 0)
        {
            history_print();
            free(line);
            continue;
        }

        free(line);
    }

    history_free();
    return 0;
}
