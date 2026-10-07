#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>

#include "job_control.h"
#include "jobs.h"

/*
 * The shell's own process group ID.  Saved at init so we can
 * reclaim the terminal after a foreground job finishes / stops.
 */
static pid_t shell_pgid;

/*
 * Parse a job ID from a command argument.
 * Accepts either "%N" or plain "N".
 * Returns the job ID on success, or -1 on parse error.
 */
static int parse_job_id(const char *arg)
{
    if (arg == NULL)
    {
        return -1;
    }

    const char *p = arg;
    if (*p == '%')
    {
        p++;
    }

    char *end = NULL;
    errno = 0;
    long val = strtol(p, &end, 10);

    if (errno != 0 || end == p || *end != '\0' || val <= 0)
    {
        return -1;
    }

    return (int)val;
}

/*
 * Wait for a foreground job (process group) with WUNTRACED.
 * If the job stops, returns 1.  If the job exits, returns 0.
 * On error returns -1.
 */
static int wait_for_job(job_t *j)
{
    int status;
    pid_t pid;
    int stopped = 0;

    /*
     * Wait for processes in this job.  We use the negative PGID
     * form so we catch all processes in the group.  WUNTRACED lets
     * us detect Ctrl+Z.
     */
    while (!stopped)
    {
        pid = waitpid(-j->pgid, &status, WUNTRACED);

        if (pid < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            if (errno == ECHILD)
            {
                /* No more children in this group */
                break;
            }
            perror("waitpid");
            return -1;
        }

        if (WIFSTOPPED(status))
        {
            job_set_state(j->id, JOB_STOPPED);
            stopped = 1;
        }
        else if (WIFEXITED(status) || WIFSIGNALED(status))
        {
            /*
             * Mark this PID as reaped.  When all PIDs
             * in the pipeline are reaped, mark the job done.
             */
            for (int i = 0; i < j->pid_count; i++)
            {
                if (j->pids[i] == pid)
                {
                    j->pids[i] = -1;
                    break;
                }
            }

            int all_done = 1;
            for (int i = 0; i < j->pid_count; i++)
            {
                if (j->pids[i] > 0)
                {
                    all_done = 0;
                    break;
                }
            }

            if (all_done)
            {
                job_set_state(j->id, JOB_DONE);
                break;
            }
        }
    }

    return stopped ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/*                         Public API                                  */
/* ------------------------------------------------------------------ */

/*
 * Initialize job control.
 *
 * 1. Make the shell the leader of its own process group.
 * 2. Take control of the terminal.
 * 3. Ignore job-control signals in the shell process so that
 *    Ctrl+Z / Ctrl+C only affect the foreground child group.
 */
void job_control_init(void)
{
    /* Make sure stdin is a terminal */
    if (!isatty(STDIN_FILENO))
    {
        return;
    }

    /* Loop until we are in the foreground. */
    while (tcgetpgrp(STDIN_FILENO) != (shell_pgid = getpgrp()))
    {
        kill(-shell_pgid, SIGTTIN);
    }

    /* Ignore interactive signals in the shell process */
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGINT,  SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    /* Put the shell in its own process group */
    shell_pgid = getpid();
    if (setpgid(shell_pgid, shell_pgid) < 0)
    {
        perror("setpgid");
    }

    /* Grab control of the terminal */
    tcsetpgrp(STDIN_FILENO, shell_pgid);

    /*
     * Install SIGCHLD handler that does NOT use SA_NOCLDSTOP,
     * so we are notified when children stop (Ctrl+Z).
     * The handler itself is lightweight — actual reaping happens
     * in job_update_status() and wait_for_job().
     */
    struct sigaction sa;
    sa.sa_handler = SIG_DFL;  /* We rely on explicit waitpid, not a handler */
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);
}

/*
 * Return the shell's PGID (needed by the executor).
 */
pid_t job_control_shell_pgid(void)
{
    return shell_pgid;
}

/*
 * The `fg` built-in: bring a job to the foreground.
 *
 * Usage: fg %N  or  fg N
 * If no argument, brings the most recent job.
 */
int builtin_fg(command_t *cmd)
{
    if (cmd == NULL)
    {
        return 0;
    }

    int job_id = -1;

    if (cmd->argc >= 2)
    {
        job_id = parse_job_id(cmd->argv[1]);
    }
    else
    {
        /* Find the highest-numbered active job */
        for (int i = MAX_JOBS - 1; i >= 0; i--)
        {
            job_t *j = job_find_by_id(i + 1);
            if (j != NULL && j->state != JOB_DONE)
            {
                job_id = j->id;
                break;
            }
        }
        /* Try iterating all slots if numbered search didn't work */
        if (job_id < 0)
        {
            /* Scan the table for any active job */
            for (int i = 0; i < MAX_JOBS; i++)
            {
                job_t *j = job_find_by_id(i + 1);
                if (j != NULL && j->state != JOB_DONE)
                {
                    if (j->id > job_id)
                    {
                        job_id = j->id;
                    }
                }
            }
        }
    }

    if (job_id < 0)
    {
        fprintf(stderr, "fg: no current job\n");
        return 0;
    }

    job_t *j = job_find_by_id(job_id);
    if (j == NULL)
    {
        fprintf(stderr, "fg: %%%d: no such job\n", job_id);
        return 0;
    }

    /* Print the job being foregrounded */
    printf("[%d] %s\n", j->id, j->cmdline);

    /* Give the job's process group the terminal */
    if (tcsetpgrp(STDIN_FILENO, j->pgid) < 0)
    {
        perror("tcsetpgrp (fg to job)");
    }

    /* Send SIGCONT in case the job was stopped */
    if (kill(-j->pgid, SIGCONT) < 0)
    {
        perror("kill (SIGCONT)");
    }

    job_set_state(j->id, JOB_RUNNING);

    /* Wait for the job */
    int stopped = wait_for_job(j);

    /* Reclaim the terminal for the shell */
    tcsetpgrp(STDIN_FILENO, shell_pgid);

    if (stopped)
    {
        printf("\n[%d]  Stopped                 %s\n", j->id, j->cmdline);
        j->notified = 1;  /* Prevent duplicate from job_notify() */
    }
    else
    {
        /* Job completed — remove it */
        job_remove(j->id);
    }

    return 0;
}

/*
 * The `bg` built-in: resume a stopped job in the background.
 *
 * Usage: bg %N  or  bg N
 * If no argument, resumes the most recent stopped job.
 */
int builtin_bg(command_t *cmd)
{
    if (cmd == NULL)
    {
        return 0;
    }

    int job_id = -1;

    if (cmd->argc >= 2)
    {
        job_id = parse_job_id(cmd->argv[1]);
    }
    else
    {
        /* Find the highest-numbered stopped job */
        for (int i = MAX_JOBS - 1; i >= 0; i--)
        {
            job_t *j = job_find_by_id(i + 1);
            if (j != NULL && j->state == JOB_STOPPED)
            {
                job_id = j->id;
                break;
            }
        }
    }

    if (job_id < 0)
    {
        fprintf(stderr, "bg: no current job\n");
        return 0;
    }

    job_t *j = job_find_by_id(job_id);
    if (j == NULL)
    {
        fprintf(stderr, "bg: %%%d: no such job\n", job_id);
        return 0;
    }

    if (j->state != JOB_STOPPED)
    {
        fprintf(stderr, "bg: job %%%d is not stopped\n", job_id);
        return 0;
    }

    /* Resume the job */
    printf("[%d] %s &\n", j->id, j->cmdline);

    if (kill(-j->pgid, SIGCONT) < 0)
    {
        perror("kill (SIGCONT)");
    }

    job_set_state(j->id, JOB_RUNNING);

    return 0;
}

/*
 * The `jobs` built-in: update status and print all active jobs.
 */
int builtin_jobs(command_t *cmd)
{
    (void)cmd;
    job_update_status();
    job_print_all();
    return 0;
}
