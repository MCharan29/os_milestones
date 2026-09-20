#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
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
 * Redirect stdin from /dev/null.
 * Used in background children that have no explicit input redirection,
 * so they do not inadvertently read from the terminal.
 */
static int redirect_stdin_devnull(void)
{
    int fd = open("/dev/null", O_RDONLY);
    if (fd < 0)
    {
        perror("/dev/null");
        return -1;
    }

    if (dup2(fd, STDIN_FILENO) < 0)
    {
        perror("dup2");
        close_fd(&fd);
        return -1;
    }

    close_fd(&fd);
    return 0;
}

/*
 * Block SIGCHLD and save the previous signal mask.
 * Used around foreground fork+wait sequences to prevent the SIGCHLD
 * handler from accidentally reaping a foreground child.
 */
static void block_sigchld(sigset_t *old)
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, old);
}

/*
 * Restore a previously saved signal mask.
 */
static void restore_sigmask(const sigset_t *old)
{
    sigprocmask(SIG_SETMASK, old, NULL);
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
 * SIGCHLD handler: reap terminated background children.
 * Uses only async-signal-safe operations and preserves errno.
 */
static void sigchld_handler(int sig)
{
    (void)sig;
    int saved_errno = errno;

    while (waitpid(-1, NULL, WNOHANG) > 0)
    {
        /* Reap all available terminated children */
    }

    errno = saved_errno;
}

/*
 * Install the SIGCHLD handler using sigaction().
 * SA_RESTART prevents interrupted slow syscalls from failing with EINTR.
 * SA_NOCLDSTOP avoids signals when children are merely stopped.
 */
void install_sigchld_handler(void)
{
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    if (sigaction(SIGCHLD, &sa, NULL) < 0)
    {
        perror("sigaction");
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
 *
 * Background commands:
 *   - stdin is redirected from /dev/null unless explicit input redirection
 *   - the child PID is reported
 *   - the parent does not wait
 *
 * Foreground commands:
 *   - SIGCHLD is blocked to prevent the handler from reaping the child
 *   - the parent waits and returns the exit status
 */
int execute_command(command_t *cmd)
{
    if (cmd == NULL || cmd->argc <= 0 || cmd->argv[0] == NULL)
    {
        return -1;
    }

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

    int is_fg = !cmd->background;
    sigset_t old_mask;

    /* Block SIGCHLD for foreground commands to protect waitpid */
    if (is_fg)
    {
        block_sigchld(&old_mask);
    }

    fflush(stdout);

    pid_t pid = fork();

    if (pid < 0)
    {
        perror("fork");
        if (is_fg)
        {
            restore_sigmask(&old_mask);
        }
        return -1;
    }

    if (pid == 0)
    {
        /* Restore default signal mask in child */
        if (is_fg)
        {
            restore_sigmask(&old_mask);
        }

        /*
         * Background: redirect stdin from /dev/null when the user
         * did not specify explicit input redirection.
         */
        if (cmd->background && cmd->input[0] == '\0')
        {
            if (redirect_stdin_devnull() < 0)
            {
                _exit(1);
            }
        }

        /* Apply I/O redirections */
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

    /* Parent: background — report PID and return immediately */
    if (cmd->background)
    {
        fprintf(stderr, "[Background PID: %d]\n", (int)pid);
        return 0;
    }

    /* Foreground: wait for child process and return exit status */
    int status = 0;
    if (wait_one(pid, &status) < 0)
    {
        perror("waitpid");
        restore_sigmask(&old_mask);
        return -1;
    }

    restore_sigmask(&old_mask);
    return status_to_exit_code(status);
}

/*
 * Execute a pipeline of one or more commands.
 *
 * Background pipelines:
 *   - the first command's stdin is redirected from /dev/null when
 *     no explicit input redirection exists
 *   - the PID of the first process is reported
 *   - the parent does not wait
 *
 * Foreground pipelines:
 *   - SIGCHLD is blocked to prevent the handler from reaping children
 *   - all children are waited for
 *   - the exit status of the final command is returned
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

    /* Block SIGCHLD for foreground pipelines to protect waitpid */
    sigset_t old_mask;
    if (!is_background)
    {
        block_sigchld(&old_mask);
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
            if (!is_background)
            {
                restore_sigmask(&old_mask);
            }
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

            if (!is_background)
            {
                restore_sigmask(&old_mask);
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

            if (!is_background)
            {
                restore_sigmask(&old_mask);
            }
            return -1;
        }

        if (pid == 0)
        {
            /* Restore default signal mask in child */
            if (!is_background)
            {
                restore_sigmask(&old_mask);
            }

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
             * Background: redirect stdin from /dev/null for the first
             * command when there is no explicit input redirection and
             * stdin is not already wired to a pipe.
             */
            if (is_background && i == 0 && cmd->input[0] == '\0')
            {
                if (redirect_stdin_devnull() < 0)
                {
                    _exit(1);
                }
            }

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
     * Background: report first process PID and return immediately.
     */
    if (is_background)
    {
        fprintf(stderr, "[Background PID: %d]\n", (int)pids[0]);
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

    restore_sigmask(&old_mask);
    return last_status;
}

/*
 * Legacy external command runner (retained for backward compatibility).
 */
void execute_external(command_t *cmd)
{
    (void)execute_command(cmd);
}
