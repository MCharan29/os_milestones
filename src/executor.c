#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "executor.h"
#include "builtin.h"

/*
 * Convert child wait status to shell exit code (0..255).
 */
static int status_to_exit_code(int status)
{
    if (WIFEXITED(status))
    {
        return WEXITSTATUS(status);
    }

    if (WIFSIGNALED(status))
    {
        return 128 + WTERMSIG(status);
    }

    return -1;
}

/*
 * Safely close a file descriptor and mark it closed (-1).
 */
static void close_fd(int *fd)
{
    if (fd == NULL || *fd < 0)
    {
        return;
    }

    while (close(*fd) < 0)
    {
        if (errno != EINTR)
        {
            break;
        }
    }

    *fd = -1;
}

/*
 * Close all pipe file descriptors in an array.
 */
static void close_all_pipes(int pipes[][2], int pipe_count)
{
    if (pipes == NULL || pipe_count <= 0)
    {
        return;
    }

    for (int i = 0; i < pipe_count; ++i)
    {
        close_fd(&pipes[i][0]);
        close_fd(&pipes[i][1]);
    }
}

/*
 * Wait for a specific child PID, automatically retrying on EINTR.
 */
static int wait_one(pid_t pid, int *status)
{
    if (pid <= 0 || status == NULL)
    {
        return -1;
    }

    for (;;)
    {
        pid_t rc = waitpid(pid, status, 0);

        if (rc == pid)
        {
            return 0;
        }

        if (rc < 0 && errno == EINTR)
        {
            continue;
        }

        return -1;
    }
}

/*
 * Reap any terminated background processes to avoid accumulating zombies.
 */
static void reap_background_jobs(void)
{
    while (waitpid(-1, NULL, WNOHANG) > 0)
    {
        /* Keep reaping until no more terminated children remain */
    }
}

/*
 * Apply file redirections (<, >, >>) for a command stage.
 * Returns 0 on success, or -1 on error with perror already invoked.
 */
static int apply_redirections(const command_t *cmd)
{
    if (cmd == NULL)
    {
        return -1;
    }

    if (cmd->input[0] != '\0')
    {
        int in_fd = open(cmd->input, O_RDONLY);
        if (in_fd < 0)
        {
            perror(cmd->input);
            return -1;
        }

        if (dup2(in_fd, STDIN_FILENO) < 0)
        {
            perror("dup2");
            close_fd(&in_fd);
            return -1;
        }

        close_fd(&in_fd);
    }

    if (cmd->output[0] != '\0')
    {
        int flags = O_WRONLY | O_CREAT | (cmd->append ? O_APPEND : O_TRUNC);
        int out_fd = open(cmd->output, flags, 0644);
        if (out_fd < 0)
        {
            perror(cmd->output);
            return -1;
        }

        if (dup2(out_fd, STDOUT_FILENO) < 0)
        {
            perror("dup2");
            close_fd(&out_fd);
            return -1;
        }

        close_fd(&out_fd);
    }

    return 0;
}

/*
 * Execute a single command.
 *
 * For plain built-in commands (no redirection, foreground), executes in-process
 * to modify shell state (e.g. cd).
 * For external commands or commands with redirection / background, executes
 * in a forked child process with execvp() or child builtin dispatch.
 */
int execute_command(command_t *cmd)
{
    if (cmd == NULL || cmd->argc <= 0 || cmd->argv[0] == NULL)
    {
        return -1;
    }

    /* Reap any background jobs */
    reap_background_jobs();

    /*
     * Plain built-in commands with no redirection running in foreground
     * execute in the shell process.
     */
    if (is_builtin(cmd->argv[0]) &&
        cmd->input[0] == '\0' &&
        cmd->output[0] == '\0' &&
        cmd->background == 0)
    {
        return execute_builtin(cmd);
    }

    fflush(stdout);

    pid_t pid = fork();

    if (pid < 0)
    {
        perror("fork");
        return -1;
    }

    if (pid == 0)
    {
        /* Child: apply I/O redirections */
        if (apply_redirections(cmd) < 0)
        {
            _exit(1);
        }

        /* Builtin inside child (e.g. echo with redirection) */
        if (is_builtin(cmd->argv[0]))
        {
            int rc = execute_builtin(cmd);
            fflush(stdout);
            _exit(rc == 0 ? 0 : rc);
        }

        /* External command replacement */
        execvp(cmd->argv[0], cmd->argv);
        perror("execvp");
        _exit(127);
    }

    /* Parent: if background (&), do not wait */
    if (cmd->background)
    {
        return 0;
    }

    /* Wait for child process and return exit status */
    int status = 0;
    if (wait_one(pid, &status) < 0)
    {
        perror("waitpid");
        return -1;
    }

    return status_to_exit_code(status);
}

/*
 * Execute a pipeline of one or more commands.
 */
int execute_pipeline(pipeline_t *pipeline)
{
    if (pipeline == NULL || pipeline->command_count <= 0)
    {
        return -1;
    }

    /* If only one command, execute it directly via execute_command() */
    if (pipeline->command_count == 1)
    {
        return execute_command(&pipeline->commands[0]);
    }

    /* Reap any background children from previous commands */
    reap_background_jobs();

    int command_count = pipeline->command_count;
    if (command_count > MAX_COMMANDS)
    {
        fprintf(stderr, "executor: pipeline exceeds maximum commands (%d)\n", MAX_COMMANDS);
        return -1;
    }

    /* Check whether background execution (&) was requested */
    int is_background = 0;
    for (int i = 0; i < command_count; ++i)
    {
        if (pipeline->commands[i].background)
        {
            is_background = 1;
            break;
        }
    }

    /* Allocate and create pipes for multi-stage pipelines */
    int pipes[MAX_COMMANDS - 1][2];
    const int pipe_count = command_count - 1;

    for (int i = 0; i < MAX_COMMANDS - 1; ++i)
    {
        pipes[i][0] = -1;
        pipes[i][1] = -1;
    }

    for (int i = 0; i < pipe_count; ++i)
    {
        if (pipe(pipes[i]) < 0)
        {
            perror("pipe");
            close_all_pipes(pipes, i);
            return -1;
        }
    }

    pid_t pids[MAX_COMMANDS];
    int forked = 0;

    for (int i = 0; i < command_count; ++i)
    {
        command_t *cmd = &pipeline->commands[i];

        if (cmd->argc <= 0 || cmd->argv[0] == NULL)
        {
            fprintf(stderr, "executor: empty command at pipeline index %d\n", i);
            close_all_pipes(pipes, pipe_count);

            for (int j = 0; j < forked; ++j)
            {
                int ignored = 0;
                (void)wait_one(pids[j], &ignored);
            }

            return -1;
        }

        /* Flush user-space output buffer before forking */
        fflush(stdout);

        pid_t pid = fork();

        if (pid < 0)
        {
            perror("fork");
            close_all_pipes(pipes, pipe_count);

            for (int j = 0; j < forked; ++j)
            {
                int ignored = 0;
                (void)wait_one(pids[j], &ignored);
            }

            return -1;
        }

        if (pid == 0)
        {
            /*
             * Pipe wiring:
             * - Intermediate / last stages read from previous pipe
             * - First / intermediate stages write to next pipe
             */
            if (i > 0)
            {
                if (dup2(pipes[i - 1][0], STDIN_FILENO) < 0)
                {
                    perror("dup2");
                    _exit(1);
                }
            }

            if (i < command_count - 1)
            {
                if (dup2(pipes[i][1], STDOUT_FILENO) < 0)
                {
                    perror("dup2");
                    _exit(1);
                }
            }

            /* Close all pipe file descriptors in the child */
            close_all_pipes(pipes, pipe_count);

            /*
             * Apply file redirections (<, >, >>).
             * Redirection is applied after pipe wiring so explicit
             * redirection takes precedence over pipeline descriptors.
             */
            if (apply_redirections(cmd) < 0)
            {
                _exit(1);
            }

            /*
             * Execute built-in commands inside child process when
             * running in a pipeline or with redirection.
             */
            if (is_builtin(cmd->argv[0]))
            {
                int rc = execute_builtin(cmd);
                fflush(stdout);
                _exit(rc == 0 ? 0 : rc);
            }

            /* Execute external command */
            execvp(cmd->argv[0], cmd->argv);

            /* Never fall back into shell code after execvp() fails */
            perror("execvp");
            _exit(127);
        }

        pids[forked++] = pid;
    }

    /*
     * The parent shell must close all pipe ends so that readers receive
     * EOF when writers terminate.
     */
    close_all_pipes(pipes, pipe_count);

    /*
     * If running in the background (&), do not block. Return immediately.
     */
    if (is_background)
    {
        return 0;
    }

    /*
     * For foreground execution, wait for all children to complete.
     * Return the exit code of the final command in the pipeline.
     */
    int last_status = 0;

    for (int i = 0; i < forked; ++i)
    {
        int status = 0;

        if (wait_one(pids[i], &status) < 0)
        {
            perror("waitpid");
            last_status = -1;
        }
        else if (i == command_count - 1)
        {
            last_status = status_to_exit_code(status);
        }
    }

    return last_status;
}

/*
 * Legacy external command runner (retained for backward compatibility).
 */
void execute_external(command_t *cmd)
{
    (void)execute_command(cmd);
}
