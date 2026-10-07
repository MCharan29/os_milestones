#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "builtin.h"

static int builtin_cd(command_t *cmd)
{
    if (cmd == NULL)
    {
        return 0;
    }

    const char *dir;

    if (cmd->argc == 1)
    {
        // No argument: go to the home directory
        dir = getenv("HOME");
        if (dir == NULL)
        {
            printf("cd: HOME not set\n");
            return 0;
        }
    }
    else if (cmd->argc == 2)
    {
        dir = cmd->argv[1];
    }
    else
    {
        printf("cd: too many arguments\n");
        return 0;
    }

    if (chdir(dir) != 0)
    {
        perror("cd");
    }

    return 0;
}

static int builtin_pwd(command_t *cmd)
{
    if (cmd == NULL)
    {
        return 0;
    }

    if (cmd->argc > 1)
    {
        printf("pwd: too many arguments\n");
        return 0;
    }

    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd)) == NULL)
    {
        perror("pwd");
    }
    else
    {
        printf("%s\n", cwd);
        printf("2500030215\n");
    }

    return 0;
}

static int builtin_echo(command_t *cmd)
{
    if (cmd == NULL)
    {
        return 0;
    }

    for (int i = 1; i < cmd->argc; i++)
    {
        if (i > 1)
        {
            printf(" ");
        }
        printf("%s", cmd->argv[i]);
    }
    printf("\n");

    return 0;
}

static int builtin_exit(command_t *cmd)
{
    if (cmd == NULL)
    {
        return 0;
    }

    if (cmd->argc > 1)
    {
        printf("exit: too many arguments\n");
        return 0;
    }

    // Tell the main loop to end the shell (no message printed)
    return 1;
}

int is_builtin(const char *name)
{
    if (name == NULL)
    {
        return 0;
    }

    return strcmp(name, "cd") == 0
        || strcmp(name, "pwd") == 0
        || strcmp(name, "echo") == 0
        || strcmp(name, "exit") == 0
        || strcmp(name, "jobs") == 0
        || strcmp(name, "fg") == 0
        || strcmp(name, "bg") == 0;
}

int execute_builtin(command_t *cmd)
{
    if (cmd == NULL || cmd->argc == 0 || cmd->argv[0] == NULL)
    {
        return 0;
    }

    if (strcmp(cmd->argv[0], "cd") == 0)
    {
        return builtin_cd(cmd);
    }
    if (strcmp(cmd->argv[0], "pwd") == 0)
    {
        return builtin_pwd(cmd);
    }
    if (strcmp(cmd->argv[0], "echo") == 0)
    {
        return builtin_echo(cmd);
    }
    if (strcmp(cmd->argv[0], "exit") == 0)
    {
        return builtin_exit(cmd);
    }
    if (strcmp(cmd->argv[0], "jobs") == 0)
    {
        return builtin_jobs(cmd);
    }
    if (strcmp(cmd->argv[0], "fg") == 0)
    {
        return builtin_fg(cmd);
    }
    if (strcmp(cmd->argv[0], "bg") == 0)
    {
        return builtin_bg(cmd);
    }

    // Not a builtin; caller should not have called us
    return 0;
}
