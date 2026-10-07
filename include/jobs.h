#ifndef JOBS_H
#define JOBS_H

#include <sys/types.h>
#include "parser.h"

#define MAX_JOBS 64
#define MAX_CMDLINE 256

typedef enum {
    JOB_RUNNING,     /* Background, actively executing */
    JOB_STOPPED,     /* Suspended by SIGTSTP / Ctrl+Z  */
    JOB_DONE         /* Exited / signaled, awaiting notification */
} job_state_t;

typedef struct {
    int         id;                      /* Job number [1], [2], ...        */
    pid_t       pgid;                    /* Process-group ID (= leader PID) */
    pid_t       pids[MAX_COMMANDS];      /* All PIDs in the pipeline        */
    int         pid_count;               /* Number of processes             */
    job_state_t state;                   /* Current state                   */
    int         notified;                /* 1 if DONE has been reported     */
    char        cmdline[MAX_CMDLINE];    /* Original command string         */
} job_t;

/*
 * Add a new job to the jobs table.
 * Returns the assigned job ID (>= 1), or -1 on error.
 */
int job_add(pid_t pgid, pid_t pids[], int pid_count, const char *cmdline);

/*
 * Remove a job from the table by its job ID.
 */
void job_remove(int id);

/*
 * Find a job by its job ID.  Returns NULL if not found.
 */
job_t *job_find_by_id(int id);

/*
 * Find a job by its process-group ID.  Returns NULL if not found.
 */
job_t *job_find_by_pgid(pid_t pgid);

/*
 * Set a job's state.
 */
void job_set_state(int id, job_state_t state);

/*
 * Print all active jobs (the `jobs` built-in output).
 */
void job_print_all(void);

/*
 * Non-blocking waitpid sweep: update states for all tracked jobs.
 * Marks finished jobs as JOB_DONE and stopped jobs as JOB_STOPPED.
 */
void job_update_status(void);

/*
 * Print notifications for completed/stopped background jobs
 * and remove DONE jobs that have been notified.
 * Should be called before each prompt.
 */
void job_notify(void);

#endif /* JOBS_H */
