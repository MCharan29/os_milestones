#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "parser.h"

void execute_external(command_t *cmd);                 /* fork + execvp + waitpid */

#endif /* EXECUTOR_H */
