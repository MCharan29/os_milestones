#ifndef JOB_CONTROL_H
#define JOB_CONTROL_H

#include "parser.h"

/*
 * Initialize job control: set up the shell's own process group,
 * take the terminal, and install signal handlers (SIGTSTP, SIGTTOU,
 * SIGTTIN, SIGINT ignored in the shell; SIGCHLD configured for
 * WUNTRACED).
 *
 * Must be called once during shell initialization, before the REPL loop.
 */
void job_control_init(void);

/*
 * Bring a job to the foreground.
 * Gives the job's process group the terminal, sends SIGCONT,
 * waits for it (WUNTRACED), then reclaims the terminal.
 *
 * Returns 0 on success, -1 on error.
 */
int builtin_fg(command_t *cmd);

/*
 * Resume a stopped job in the background.
 * Sends SIGCONT to the job's process group and marks it RUNNING.
 *
 * Returns 0 on success, -1 on error.
 */
int builtin_bg(command_t *cmd);

/*
 * The `jobs` built-in: update status and print all active jobs.
 *
 * Returns 0.
 */
int builtin_jobs(command_t *cmd);

#endif /* JOB_CONTROL_H */
