#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "executor.h"

void execute_external(command_t *cmd)
{
    if (cmd == NULL || cmd->argc == 0 || cmd->argv[0] == NULL)
    {
        return;
    }

    pid_t pid = fork();

    if (pid == 0)
    {
        // Child: replace this process with the command
        execvp(cmd->argv[0], cmd->argv);
        perror("execvp");
        exit(1);
    }
    else if (pid > 0)
    {
        // Parent: wait for the child to finish
        int status;
        waitpid(pid, &status, 0);
    }
    else
    {
        perror("fork");
    }
}
