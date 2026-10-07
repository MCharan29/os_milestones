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
#include "builtin.h"
#include "executor.h"
#include "jobs.h"
#include "job_control.h"

int main(void)
{
    /* Initialize job control: process group, terminal, signals */
    job_control_init();

    printf("=====================================\n");
    printf("      Shellforge (Job Control Update 2)\n");
    printf(" A Unix Style Shell written in C\n");
    printf("=====================================\n");

    char *line;
    token_list_t tokens;
    pipeline_t pipeline;

    while (1)
    {
        /* Notify about completed/stopped background jobs before prompt */
        job_notify();

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

        // Custom history command: handled in-process, never forked
        if (strcmp(line, "history") == 0)
        {
            history_print();
            free(line);
            continue;
        }

        if (lexer(line, &tokens) == 0)
        {
            token_print(&tokens);

            if (parse(&tokens, &pipeline))
            {
                expand_variables(&pipeline);
                pipeline_print(&pipeline);

                // Built-ins run in the shell process, after the token/pipeline
                // report, but only when there is no redirection or background
                // (those need a forked child so the redirection takes effect)
                if (pipeline.command_count == 1 &&
                    pipeline.commands[0].argc > 0 &&
                    is_builtin(pipeline.commands[0].argv[0]) &&
                    pipeline.commands[0].input[0] == '\0' &&
                    pipeline.commands[0].output[0] == '\0' &&
                    pipeline.commands[0].background == 0)
                {
                    // execute_builtin returns 1 to exit the shell
                    if (execute_builtin(&pipeline.commands[0]))
                    {
                        free(line);
                        break;
                    }
                }
                else if (pipeline.command_count > 1 ||
                         pipeline.commands[0].argc > 0)
                {
                    // Pipelines run via pipe/dup2/fork/execvp/waitpid.
                    // Pass the original command line for the jobs table.
                    execute_pipeline(&pipeline, line);
                }
            }
        }

        free(line);
    }

    history_free();
    return 0;
}
