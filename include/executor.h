#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <sys/types.h>
#include "parser.h"

/*
 * Return the shell's own process group ID.
 * Defined in job_control.c.
 */
pid_t job_control_shell_pgid(void);

/*
 * Execute a single command.
 *
 * Plain built-in commands (no redirection, foreground) execute in-process.
 * External commands, commands with redirection, and background commands
 * execute in a child process.
 *
 * Returns the command exit status (0-255), or -1 on error.
 */
int execute_command(command_t *cmd, const char *cmdline);

/*
 * Execute a pipeline of one or more commands.
 *
 * Handles:
 *   - Process creation with fork()
 *   - Program execution with execvp()
 *   - Multi-stage pipelines with pipe() and dup2()
 *   - File redirection (<, >, >>) with open() and dup2()
 *   - Background execution (&) with jobs table integration
 *   - Foreground wait with WUNTRACED for Ctrl+Z support
 *   - Terminal control via tcsetpgrp()
 *   - Process group creation via setpgid()
 *   - Proper descriptor closing in parent and children
 *
 * cmdline: the original command-line string, stored in the jobs table.
 *
 * Returns the exit status of the final pipeline stage, or -1 on error.
 */
int execute_pipeline(pipeline_t *pipeline, const char *cmdline);

#endif /* EXECUTOR_H */
