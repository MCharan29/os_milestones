#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "executor.h"
#include "builtin.h"
#include "jobs.h"

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
 * Reset job-control signals to default in child processes.
 * The shell ignores SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU
 * so that they only affect the foreground job.  Children must
 * restore the default actions.
 */
static void child_reset_signals(void)
{
    signal(SIGINT,  SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
}

/*
 * Execute a single command (single-stage pipeline).
 *
 * For plain built-in commands (no redirection, foreground), executes in-process
 * to modify shell state (e.g. cd).
 * For external commands or commands with redirection / background, executes
 * in a forked child process with execvp() or child builtin dispatch.
 *
 * Background commands are added to the jobs table.
 * Foreground commands support Ctrl+Z (SIGTSTP) via WUNTRACED.
 */
int execute_command(command_t *cmd, const char *cmdline)
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

    int is_bg = cmd->background;

    fflush(stdout);

    pid_t pid = fork();

    if (pid < 0)
    {
        perror("fork");
        return -1;
    }

    if (pid == 0)
    {
        /* --- Child process --- */

        /* Create a new process group with this child as the leader */
        setpgid(0, 0);

        /* If foreground, take the terminal (SIGTTOU is currently ignored) */
        if (!is_bg)
        {
            if (tcsetpgrp(STDIN_FILENO, getpid()) < 0) perror("tcsetpgrp (child pipeline fg)");
        }

        /* Reset signals to default AFTER taking the terminal */
        child_reset_signals();

        /*
         * Background: redirect stdin from /dev/null when the user
         * did not specify explicit input redirection.
         */
        if (is_bg && cmd->input[0] == '\0')
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

    /* --- Parent process --- */

    /* Set process group in parent too (avoids race with child) */
    setpgid(pid, pid);

    if (is_bg)
    {
        /* Add to jobs table and report */
        pid_t pids[] = { pid };
        int job_id = job_add(pid, pids, 1, cmdline ? cmdline : cmd->argv[0]);
        if (job_id > 0)
        {
            fprintf(stderr, "[%d] %d\n", job_id, (int)pid);
        }
        return 0;
    }

    if (tcsetpgrp(STDIN_FILENO, pid) < 0)
    {
        perror("tcsetpgrp (parent to child)");
    }

    /* Wait for child with WUNTRACED so Ctrl+Z is detected */
    int status = 0;
    pid_t wpid;

    for (;;)
    {
        wpid = waitpid(pid, &status, WUNTRACED);

        if (wpid == pid)
        {
            break;
        }

        if (wpid < 0 && errno == EINTR)
        {
            continue;
        }

        if (wpid < 0)
        {
            perror("waitpid");
            break;
        }
    }

    /* Reclaim the terminal for the shell */
    tcsetpgrp(STDIN_FILENO, job_control_shell_pgid());

    if (WIFSTOPPED(status))
    {
        /* Child was stopped (Ctrl+Z) — add to jobs table as stopped */
        pid_t pids[] = { pid };
        int job_id = job_add(pid, pids, 1, cmdline ? cmdline : cmd->argv[0]);
        if (job_id > 0)
        {
            job_set_state(job_id, JOB_STOPPED);
            printf("\n[%d]  Stopped                 %s\n", job_id, cmdline ? cmdline : cmd->argv[0]);
            job_t *sj = job_find_by_id(job_id);
            if (sj) sj->notified = 1;
        }
        return 128 + WSTOPSIG(status);
    }

    return status_to_exit_code(status);
}

/*
 * Execute a pipeline of one or more commands.
 *
 * Background pipelines are added to the jobs table.
 * Foreground pipelines support Ctrl+Z via WUNTRACED.
 * All children are placed in the same process group.
 */
int execute_pipeline(pipeline_t *pipeline, const char *cmdline)
{
    if (pipeline == NULL || pipeline->command_count <= 0)
    {
        return -1;
    }

    /* If only one command, delegate to execute_command() */
    if (pipeline->command_count == 1)
    {
        return execute_command(&pipeline->commands[0], cmdline);
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
    pid_t pgid = 0;  /* Process group = PID of the first child */

    for (int i = 0; i < command_count; ++i)
    {
        command_t *cmd = &pipeline->commands[i];

        if (cmd->argc <= 0 || cmd->argv[0] == NULL)
        {
            fprintf(stderr, "executor: empty command at pipeline index %d\n", i);
            close_all_pipes(pipes, pipe_count);

            /* Clean up already-forked children */
            for (int j = 0; j < forked; ++j)
            {
                int ignored = 0;
                waitpid(pids[j], &ignored, 0);
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
                waitpid(pids[j], &ignored, 0);
            }
            return -1;
        }

        if (pid == 0)
        {
            /* --- Child process --- */

            /* All children in the pipeline share the same process group */
            if (i == 0)
            {
                setpgid(0, 0);  /* First child: create new group */
            }
            else
            {
                setpgid(0, pgid);  /* Join the first child's group */
            }

            /* If foreground, take the terminal (SIGTTOU is currently ignored) */
            if (!is_background && i == 0)
            {
                if (tcsetpgrp(STDIN_FILENO, getpid()) < 0) perror("tcsetpgrp (child pipeline fg)");
            }

            /* Reset signals to default AFTER taking the terminal */
            child_reset_signals();

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

        /* --- Parent --- */

        /* Set process group in parent (avoids race) */
        if (i == 0)
        {
            pgid = pid;
        }
        setpgid(pid, pgid);

        pids[forked++] = pid;
    }

    /*
     * The parent shell must close all pipe ends so that readers receive
     * EOF when writers terminate.
     */
    close_all_pipes(pipes, pipe_count);

    /*
     * Background: add to jobs table and return immediately.
     */
    if (is_background)
    {
        int job_id = job_add(pgid, pids, forked, cmdline ? cmdline : "pipeline");
        if (job_id > 0)
        {
            fprintf(stderr, "[%d] %d\n", job_id, (int)pgid);
        }
        return 0;
    }

    /*
     * Foreground: give the pipeline's process group the terminal.
     */
    tcsetpgrp(STDIN_FILENO, pgid);

    /*
     * Wait for all children.  Use WUNTRACED so we detect Ctrl+Z.
     */
    int last_status = 0;
    int stopped = 0;

    for (int i = 0; i < forked; ++i)
    {
        int status = 0;
        pid_t wpid;

        for (;;)
        {
            wpid = waitpid(pids[i], &status, WUNTRACED);

            if (wpid == pids[i])
            {
                break;
            }

            if (wpid < 0 && errno == EINTR)
            {
                continue;
            }

            if (wpid < 0)
            {
                perror("waitpid");
                last_status = -1;
                goto wait_done;
            }
        }

        if (WIFSTOPPED(status))
        {
            stopped = 1;
            /* Don't wait for remaining processes — they're all stopped */
            break;
        }
        else if (i == command_count - 1)
        {
            last_status = status_to_exit_code(status);
        }
    }

wait_done:
    /* Reclaim the terminal for the shell */
    tcsetpgrp(STDIN_FILENO, job_control_shell_pgid());

    if (stopped)
    {
        /* Pipeline was stopped (Ctrl+Z) — add to jobs table */
        int job_id = job_add(pgid, pids, forked, cmdline ? cmdline : "pipeline");
        if (job_id > 0)
        {
            job_set_state(job_id, JOB_STOPPED);
            printf("\n[%d]  Stopped                 %s\n", job_id,
                   cmdline ? cmdline : "pipeline");
            job_t *sj = job_find_by_id(job_id);
            if (sj) sj->notified = 1;
        }
        return 128 + SIGTSTP;
    }

    return last_status;
}
