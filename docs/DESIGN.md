# Shellforge - Design Specification

This document describes the design of shellforge, a teaching Unix-style shell
implemented in C11 (built with `gcc -Wall -Wextra -Iinclude`, linked against
`-lreadline`). It is intended for faculty review and covers the overall
architecture, the responsibility of each module, the execution model, the
system call layer, verification, and known limitations.

## 1. Overview

Shellforge is a small, modular command-line shell written for an operating
systems course. It runs a classic read-eval-print loop (REPL) that turns a
line of input into one or more processes:

```
readline -> lexer (tokens) -> parser (pipeline_t) -> expansion ($NAME/${NAME}) -> dispatch
```

Each input line passes through the following stages:

1. **Input** is read with GNU Readline (`readline("shellforge$ ")`) and added
   to history.
2. **Lexical analysis** converts the raw line into a token list
   (`token_list_t`), handling quotes, escapes, and operators.
3. **Parsing** converts the token list into a `pipeline_t`: up to 16 commands
   (`MAX_COMMANDS`), each with up to 128 arguments (`MAX_ARGS`), plus
   per-command `input`/`output` filenames, an `append` flag (`>>`), and a
   `background` flag (`&`).
4. **Variable expansion** rewrites `$NAME` and `${NAME}` in every argument
   using `getenv()`; unset variables expand to the empty string.
5. **Dispatch** either runs a built-in in the shell process or executes the
   pipeline with `fork()`/`execvp()`/`waitpid()`.

For teaching purposes the REPL also prints the token list and the parsed
pipeline in the exact formats required by the assignment before executing.

## 2. Module Responsibilities

### token (`include/token.h`, `src/token.c`)

Defines the token vocabulary and container. `token_type_t` enumerates
`TOKEN_WORD`, `TOKEN_PIPE`, `TOKEN_INPUT`, `TOKEN_OUTPUT`, `TOKEN_APPEND`,
`TOKEN_BACKGROUND`, and `TOKEN_END`. Tokens live in a fixed-capacity
`token_list_t` (up to `MAX_TOKENS` = 128 tokens, each up to
`MAX_TOKEN_LEN` = 128 characters). Also provides `token_print()` for the
assignment's token report (`0 : WORD ls`).

### lexer (`include/lexer.h`, `src/lexer.c`)

The character scanner. `lexer()` walks the input line, skips whitespace,
recognizes the operators `|`, `<`, `>`, `>>`, and `&`, and builds word tokens
with support for single quotes, double quotes, and backslash escapes. An
unterminated quote is reported as `Lexer Error : Unterminated ... quote`. The
list is always terminated with a `TOKEN_END` marker.

### parser (`include/parser.h`, `src/parser.c`)

Converts the token stream into the core data structure:

```c
typedef struct {
    char *argv[MAX_ARGS];      /* strdup'd copies of token text */
    int argc;
    char input[MAX_TOKEN_LEN]; /* "" if none */
    char output[MAX_TOKEN_LEN];
    int append;                /* 1 for >>, 0 for > */
    int background;            /* 1 if & */
} command_t;

typedef struct {
    command_t commands[MAX_COMMANDS];
    int command_count;
} pipeline_t;
```

`parse()` fills one `command_t` per command, attaching redirection filenames
and flags, starting a new command at each `|`. It reports
`Parser Error : ...` for a missing redirection filename or a pipeline longer
than 16 commands. `pipeline_print()` renders the assignment's pipeline report.

### expand (`include/expand.h`, `src/expand.c`)

Expands environment variables after parsing. `expand_variables()` rewrites
each `argv` entry, replacing `$NAME` and `${NAME}` with the result of
`getenv()` (the empty string when unset). A `$` not followed by a valid name
is left untouched. Buffers grow dynamically, so expansion is not limited by
`MAX_TOKEN_LEN`.

### history (`include/history.h`, `src/history.c`)

Keeps an in-memory log of entered commands (up to
`MAX_HISTORY_ENTRIES` = 100) alongside Readline's own history
(`add_history`). The `history` command prints this log; it is handled in
`main.c` and is never forked.

### builtin (`include/builtin.h`, `src/builtin.c`)

Implements the built-in commands that must run in the shell process itself:

- `cd` changes the working directory with `chdir()`, defaulting to `$HOME`
  with no argument, with graceful errors for bad directories.
- `pwd` prints the working directory from `getcwd()` (followed by the
  roll-number line required by the course).
- `echo` prints its arguments joined by single spaces.
- `exit` returns a code telling the main loop to terminate the shell.

`is_builtin()` identifies built-in names; `execute_builtin()` dispatches them
on a parsed `command_t`.

### executor (`include/executor.h`, `src/executor.c`)

Runs external commands and pipelines. `execute_pipeline()` forks one child
per command, connects consecutive commands with `pipe()` + `dup2()`, applies
`<` / `>` / `>>` redirection with `open()` + `dup2()`, replaces each child
image with `execvp()`, and collects foreground children with `waitpid()`.
This module contains the entire system call layer described in section 4.

### main (`src/main.c`)

The REPL. Prints the banner, reads lines with Readline, records history, then
runs the lex -> print -> parse -> expand -> print pipeline and dispatches:
built-ins in-process when eligible (see section 3), everything else through
`execute_pipeline()`. Ctrl+D exits with `Goodbye!`.

## 3. Execution Model

Shellforge has two execution paths, chosen in `main.c` after parsing:

- **In-process built-ins.** A built-in (`cd`, `pwd`, `echo`, `exit`) runs in
  the shell process, without `fork()`, only when the pipeline is a single
  command with no redirection and no background (`command_count == 1`, empty
  `input`/`output`, `background == 0`). This is what allows `cd` to affect
  the shell's own working directory.
- **External execution.** Everything else — multi-command pipelines, any
  command with `<` / `>` / `>>` or `&`, and all non-built-in commands — goes
  to `execute_pipeline()`, even if it is a single command whose name happens
  to match a built-in (that case is executed via `execvp()` in a child).

## 4. System Call Layer

The system call layer lives in `src/executor.c` and is the heart of the
project. It is structured into **Process Creation & Background Execution**
and **Pipeline Execution & I/O Redirection**.

### Process Creation & Background Execution

**Process creation.** One `fork()` per command in the pipeline; child PIDs
are recorded in a `pids[]` array. `fork()` failure is reported with
`perror("fork")` and no further children are launched.

**Image replacement.** Each child calls `execvp(cmd->argv[0], cmd->argv)`,
letting `execvp` search `PATH`. On failure it reports `perror("execvp")` and
exits with `exit(1)`, so a failed `execvp` never falls back into shell code.
`fflush(stdout)` before forking keeps buffered output from being duplicated
by children.

**Waiting and background.** For a foreground pipeline the parent calls
`waitpid(pid, &status, 0)` once per child. When a command or pipeline ends
with `&`:
- The child PID (or first process PID for pipelines) is printed via `[Background PID: <pid>]`.
- Stdin is redirected from `/dev/null` for commands without explicit input redirection.
- The parent does not synchronously wait and immediately returns to the shell prompt.
- Finished background children are reaped asynchronously by a dedicated `SIGCHLD` signal handler installed via `sigaction()` using non-blocking `waitpid(-1, NULL, WNOHANG)`.
- Signals are blocked around foreground child wait sequences with `sigprocmask()` to eliminate race conditions between foreground waiting and SIGCHLD reaping.

System calls: `fork`, `execvp`, `waitpid`, `sigaction`, `sigprocmask`, `open`, `dup2`, `close`, `exit`.

### Pipeline Execution & I/O Redirection

**Pipes.** The parent creates one `pipe()` between each pair of consecutive
commands (at most `MAX_COMMANDS - 1` pipes) before forking, so the
descriptors are inherited by every child. `pipe()` failure is reported with
`perror("pipe")`.

**`dup2()` wiring.** Each child first wires the pipe ends: its `stdin` is
`dup2()`ed from the previous pipe's read end (when `i > 0`) and its `stdout`
is `dup2()`ed onto the next pipe's write end (when it is not the last
command). Explicit redirection is applied **after** the pipe wiring, so an
`open()`ed file descriptor `dup2()`ed onto `stdin`/`stdout` overrides the
pipe end.

**Redirection flags.** Files are opened with:

| Operator | `open()` flags                          | Mode  |
|----------|-----------------------------------------|-------|
| `<`      | `O_RDONLY`                              | -     |
| `>`      | `O_WRONLY \| O_CREAT \| O_TRUNC`        | 0644  |
| `>>`     | `O_WRONLY \| O_CREAT \| O_APPEND`       | 0644  |

An `open()` failure is reported with `perror()` and the child exits with
`exit(1)`.

**Descriptor hygiene and EOF.** After wiring, each child closes **all**
inherited pipe file descriptors, and the parent closes each pipe end as soon
as the child that needs it has been forked. Because no process keeps a stray
write end open, a reader sees EOF as soon as the producer exits, which is
what makes pipelines like `ls | grep bin` terminate cleanly.

System calls: `pipe`, `dup2`, `open`, `close`.

**Built-in syscalls** (in `src/builtin.c`, unrelated to 4.1/4.2): `cd` uses
`chdir()` and `pwd` uses `getcwd()`; both run in-process only (see section 3).

### C wrapper -> kernel syscall mapping

Under `strace` the glibc wrappers appear under their kernel names:

| C wrapper (source) | Kernel syscall (strace) |
|--------------------|-------------------------|
| `fork()`           | `clone`                 |
| `execvp()`         | `execve`                |
| `waitpid()`        | `wait4`                 |
| `pipe()`           | `pipe2`                 |
| `open()`           | `openat`                |
| `dup2()`           | `dup2`                  |
| `close()`          | `close`                 |
| `chdir()`          | `chdir`                 |
| `getcwd()`         | `getcwd`                |

## 5. Verification

**Test suite.** `tests/run_tests.sh` provides 10 end-to-end cases that build
the project, pipe command scripts into `./shellforge`, and assert on the
output. Tests 1 and 10 cover process creation and execvp error
reporting; test 8 covers the background/`sleep` path; tests 2-7 cover
redirection and pipelines. Run it with:

```bash
make test
```

**strace demo: process creation with `sleep` and background.**

```bash
printf 'sleep 1 &\necho bg-ok\nexit\n' | strace -f -e trace=clone,execve,wait4 ./shellforge
```

Look for one `clone` call forking the child that `execve`s into `sleep`.
Notice there is no blocking `wait4` on that child before `echo bg-ok` runs
and prints immediately after — the parent does not wait because the command
ended in `&`. The child is instead reaped by the non-blocking
`wait4(-1, ..., WNOHANG, ...)` call at the top of the next
`execute_pipeline()`, triggered here by the `echo` command.

**strace demo: pipeline with redirection.**

```bash
printf 'ls / | grep bin > /tmp/sf_demo.txt\nexit\n' | strace -f -e trace=clone,execve,wait4,pipe,dup2,openat,close ./shellforge
```

Look for `pipe2` creating the pipe between the two commands, followed by two
`clone` calls (one child per command). In the children, `dup2` wires the pipe
ends onto `stdin`/`stdout`, and `openat` opens `/tmp/sf_demo.txt` with
`O_WRONLY|O_CREAT|O_TRUNC` before a final `dup2` overrides stdout. The
`execve` calls show the `PATH` search as a series of attempts failing with
`ENOENT` until the binary is found, and the parent reaps both children with
`wait4`.

**strace demo: built-ins.**

```bash
printf 'cd /tmp\npwd\nexit\n' | strace -f -e trace=chdir,getcwd ./shellforge
```

Because `cd` and `pwd` run in the shell process, the `chdir("/tmp")` and
`getcwd` calls appear on the shell's own PID, with no `clone` in between —
the clearest demonstration that built-ins execute in-process.

## 6. Known Limitations

- Built-ins are not supported inside multi-command pipelines; every stage of
  a pipeline is executed via `execvp()`.
- Redirection-only built-ins (e.g. `echo x > f`) are run via `/bin/echo` in a
  forked child, so in-process built-in semantics (such as `cd` affecting the
  shell itself) apply only to the plain form.
- Fixed limits: at most 16 commands per pipeline, 128 arguments per command,
  and 128 characters per token.
- Background jobs are not tracked: there is no job control (`fg`, `bg`) and no
  `jobs` command; however, terminated background children are automatically
  reaped via a SIGCHLD signal handler to prevent zombies.
