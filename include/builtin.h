#ifndef BUILTIN_H
#define BUILTIN_H

#include "parser.h"
#include "job_control.h"

int is_builtin(const char *name);                      /* 1 yes, 0 no */
int execute_builtin(command_t *cmd);                   /* 1 exit shell, 0 continue */

#endif /* BUILTIN_H */
