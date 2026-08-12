#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "token.h"
#include "lexer.h"
#include "history.h"

int main(void)
{
    printf("Shellforge\n");
    printf("A Unix Style Shell written in C\n\n");

    char *line;
    token_list_t list;

    while (1)
    {
        line = readline("shellforge$ ");
        if (line == NULL)
        {
            printf("\nExiting...\n");
            break;
        }

        if (strlen(line) == 0)
        {
            free(line);
            continue;
        }

        add_history(line);
        history_add(line);

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

        if (lexer(line, &list) == 0)
        {
            token_print(&list);
        }

        free(line);
    }

    history_free();
    return 0;
}
