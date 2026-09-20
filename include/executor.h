#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "parser.h"

/*
 * Legacy external command execution interface (fork + execvp + waitpid).
 * Retained for backward compatibility.
 */
void execute_external(command_t *cmd);

/*
 * Execute a single command.
 *
 * Plain built-in commands (no redirection, foreground) execute in-process.
 * External commands, commands with redirection, and background commands
 * execute in a child process.
 *
 * Returns the command exit status (0-255), or -1 on error.
 */
int execute_command(command_t *cmd);

/*
 * Execute a pipeline of one or more commands.
 *
 * Handles:
 *   - Process creation with fork()
 *   - Program execution with execvp()
 *   - Multi-stage pipelines with pipe() and dup2()
 *   - File redirection (<, >, >>) with open() and dup2()
 *   - Background execution (&) and zombie reaping
 *   - Waiting for all children with waitpid()
 *   - Proper descriptor closing in parent and children
 *
 * Returns the exit status of the final pipeline stage, or -1 on error.
 */
int execute_pipeline(pipeline_t *pipeline);

/*
 * Install a SIGCHLD handler that reaps terminated background children.
 * Must be called once during shell initialization, before the REPL loop.
 */
void install_sigchld_handler(void);

#endif /* EXECUTOR_H */
