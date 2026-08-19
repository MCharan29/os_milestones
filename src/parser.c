#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"

void command_init(command_t *cmd)
{
    if (cmd == NULL)
    {
        return;
    }

    cmd->argc = 0;
    cmd->input[0] = '\0';
    cmd->output[0] = '\0';
    cmd->append = 0;
    cmd->background = 0;

    for (int i = 0; i < MAX_ARGS; i++)
    {
        cmd->argv[i] = NULL;
    }
}

int parse(token_list_t *tokens, pipeline_t *pipeline)
{
    if (tokens == NULL || pipeline == NULL)
    {
        return 0;
    }

    pipeline->command_count = 1;
    int current = 0;
    command_init(&pipeline->commands[current]);
    int i = 0;

    while (i < tokens->count)
    {
        token_t *token = &tokens->tokens[i];
        command_t *cmd = &pipeline->commands[current];

        switch (token->type)
        {
            case TOKEN_WORD:
                cmd->argv[cmd->argc++] = strdup(token->text);
                i++;
                break;

            case TOKEN_INPUT:
                // Next token must be the filename
                if (i + 1 >= tokens->count || tokens->tokens[i + 1].type != TOKEN_WORD)
                {
                    printf("Parser Error : filename expected after '<'\n");
                    return 0;
                }
                strncpy(cmd->input, tokens->tokens[i + 1].text, MAX_TOKEN_LEN - 1);
                cmd->input[MAX_TOKEN_LEN - 1] = '\0';
                i += 2;
                break;

            case TOKEN_OUTPUT:
                // Next token must be the filename
                if (i + 1 >= tokens->count || tokens->tokens[i + 1].type != TOKEN_WORD)
                {
                    printf("Parser Error : filename expected after '>'\n");
                    return 0;
                }
                strncpy(cmd->output, tokens->tokens[i + 1].text, MAX_TOKEN_LEN - 1);
                cmd->output[MAX_TOKEN_LEN - 1] = '\0';
                cmd->append = 0;
                i += 2;
                break;

            case TOKEN_APPEND:
                // Next token must be the filename
                if (i + 1 >= tokens->count || tokens->tokens[i + 1].type != TOKEN_WORD)
                {
                    printf("Parser Error : filename expected after '>>'\n");
                    return 0;
                }
                strncpy(cmd->output, tokens->tokens[i + 1].text, MAX_TOKEN_LEN - 1);
                cmd->output[MAX_TOKEN_LEN - 1] = '\0';
                cmd->append = 1;
                i += 2;
                break;

            case TOKEN_BACKGROUND:
                cmd->background = 1;
                i++;
                break;

            case TOKEN_PIPE:
                // Terminate current command's argv and start a new one
                cmd->argv[cmd->argc] = NULL;
                current++;
                if (current >= MAX_COMMANDS)
                {
                    printf("Parser Error : too many commands in pipeline.\n");
                    return 0;
                }
                command_init(&pipeline->commands[current]);
                pipeline->command_count++;
                i++;
                break;

            case TOKEN_END:
                i++;
                break;
        }
    }

    pipeline->commands[current].argv[pipeline->commands[current].argc] = NULL;
    return 1;
}

void pipeline_print(const pipeline_t *pipeline)
{
    if (pipeline == NULL)
    {
        return;
    }

    printf("\n========== PIPELINE ==========\n");

    for (int c = 0; c < pipeline->command_count; c++)
    {
        const command_t *cmd = &pipeline->commands[c];

        printf("\nCommand %d\n", c + 1);
        printf("-----------------------------\n");
        printf("Arguments\n");
        for (int j = 0; j < cmd->argc; j++)
        {
            printf("argv[%d] = %s\n", j, cmd->argv[j]);
        }
        printf("Input      : %s\n", cmd->input[0] != '\0' ? cmd->input : "None");
        printf("Output     : %s\n", cmd->output[0] != '\0' ? cmd->output : "None");
        printf("Append     : %s\n", cmd->append ? "Yes" : "No");
        printf("Background : %s\n", cmd->background ? "Yes" : "No");
        printf("==============================\n");
    }
}
