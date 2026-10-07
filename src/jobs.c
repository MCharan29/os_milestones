#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#include "jobs.h"

/*
 * Global jobs table.
 */
static job_t jobs_table[MAX_JOBS];
static int   next_job_id = 1;

/*
 * Find a free slot in the jobs table.
 * Returns the index, or -1 if full.
 */
static int find_free_slot(void)
{
    for (int i = 0; i < MAX_JOBS; i++)
    {
        if (jobs_table[i].id == 0)
        {
            return i;
        }
    }
    return -1;
}

/*
 * Add a new job to the jobs table.
 * Returns the assigned job ID (>= 1), or -1 on error.
 */
int job_add(pid_t pgid, pid_t pids[], int pid_count, const char *cmdline)
{
    int slot = find_free_slot();
    if (slot < 0)
    {
        fprintf(stderr, "shellforge: jobs table full\n");
        return -1;
    }

    job_t *j = &jobs_table[slot];
    j->id = next_job_id++;
    j->pgid = pgid;
    j->pid_count = pid_count;
    j->state = JOB_RUNNING;
    j->notified = 0;

    for (int i = 0; i < pid_count && i < MAX_COMMANDS; i++)
    {
        j->pids[i] = pids[i];
    }

    if (cmdline != NULL)
    {
        strncpy(j->cmdline, cmdline, MAX_CMDLINE - 1);
        j->cmdline[MAX_CMDLINE - 1] = '\0';
    }
    else
    {
        j->cmdline[0] = '\0';
    }

    return j->id;
}

/*
 * Remove a job from the table by its job ID.
 */
void job_remove(int id)
{
    for (int i = 0; i < MAX_JOBS; i++)
    {
        if (jobs_table[i].id == id)
        {
            memset(&jobs_table[i], 0, sizeof(job_t));
            return;
        }
    }
}

/*
 * Find a job by its job ID.  Returns NULL if not found.
 */
job_t *job_find_by_id(int id)
{
    for (int i = 0; i < MAX_JOBS; i++)
    {
        if (jobs_table[i].id == id)
        {
            return &jobs_table[i];
        }
    }
    return NULL;
}

/*
 * Find a job by its process-group ID.  Returns NULL if not found.
 */
job_t *job_find_by_pgid(pid_t pgid)
{
    for (int i = 0; i < MAX_JOBS; i++)
    {
        if (jobs_table[i].id != 0 && jobs_table[i].pgid == pgid)
        {
            return &jobs_table[i];
        }
    }
    return NULL;
}

/*
 * Set a job's state.
 */
void job_set_state(int id, job_state_t state)
{
    job_t *j = job_find_by_id(id);
    if (j != NULL)
    {
        j->state = state;
        if (state != JOB_DONE)
        {
            j->notified = 0;
        }
    }
}

/*
 * Return a human-readable state string.
 */
static const char *state_string(job_state_t state)
{
    switch (state)
    {
        case JOB_RUNNING: return "Running";
        case JOB_STOPPED: return "Stopped";
        case JOB_DONE:    return "Done";
    }
    return "Unknown";
}

/*
 * Print all active jobs (the `jobs` built-in output).
 * Format: [N]  State                   cmdline
 */
void job_print_all(void)
{
    for (int i = 0; i < MAX_JOBS; i++)
    {
        if (jobs_table[i].id != 0)
        {
            printf("[%d]  %-24s%s\n",
                   jobs_table[i].id,
                   state_string(jobs_table[i].state),
                   jobs_table[i].cmdline);
        }
    }
}

/*
 * Non-blocking waitpid sweep: update states for all tracked jobs.
 * Marks finished jobs as JOB_DONE and stopped jobs as JOB_STOPPED.
 */
void job_update_status(void)
{
    int status;
    pid_t pid;

    /*
     * Non-blocking sweep: reap any zombie children or detect stopped ones.
     * WUNTRACED catches stopped children, WNOHANG prevents blocking.
     */
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0)
    {
        /* Find the job that owns this PID */
        for (int i = 0; i < MAX_JOBS; i++)
        {
            if (jobs_table[i].id == 0)
            {
                continue;
            }

            for (int p = 0; p < jobs_table[i].pid_count; p++)
            {
                if (jobs_table[i].pids[p] == pid)
                {
                    if (WIFSTOPPED(status))
                    {
                        jobs_table[i].state = JOB_STOPPED;
                        jobs_table[i].notified = 0;
                    }
                    else if (WIFEXITED(status) || WIFSIGNALED(status))
                    {
                        /*
                         * Mark the PID as reaped. If all PIDs in the
                         * pipeline have been reaped, mark the job DONE.
                         */
                        jobs_table[i].pids[p] = -1;

                        int all_done = 1;
                        for (int k = 0; k < jobs_table[i].pid_count; k++)
                        {
                            if (jobs_table[i].pids[k] > 0)
                            {
                                all_done = 0;
                                break;
                            }
                        }

                        if (all_done)
                        {
                            jobs_table[i].state = JOB_DONE;
                            jobs_table[i].notified = 0;
                        }
                    }

                    goto next_pid;
                }
            }
        }
next_pid:;
    }
}

/*
 * Print notifications for completed/stopped background jobs
 * and remove DONE jobs that have been notified.
 * Should be called before each prompt.
 */
void job_notify(void)
{
    job_update_status();

    for (int i = 0; i < MAX_JOBS; i++)
    {
        if (jobs_table[i].id == 0)
        {
            continue;
        }

        if (jobs_table[i].state == JOB_DONE && !jobs_table[i].notified)
        {
            printf("[%d]  %-24s%s\n",
                   jobs_table[i].id,
                   "Done",
                   jobs_table[i].cmdline);
            jobs_table[i].notified = 1;
            job_remove(jobs_table[i].id);
        }
        else if (jobs_table[i].state == JOB_STOPPED && !jobs_table[i].notified)
        {
            printf("[%d]  %-24s%s\n",
                   jobs_table[i].id,
                   "Stopped",
                   jobs_table[i].cmdline);
            jobs_table[i].notified = 1;
        }
    }
}
