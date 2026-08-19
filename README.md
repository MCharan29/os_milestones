# Shellforge - Unix-Style Shell in C

Shellforge is a lightweight, modular Unix-style command-line shell implemented in C for systems programming and operating systems course projects.

## Project Structure

```
shellforge/
├── include/
│   ├── token.h     # Token definitions, token types, and list declarations
│   ├── lexer.h     # Lexical analyzer interface
│   ├── history.h   # In-memory history tracking interface
│   ├── parser.h    # Parser structures and pipeline interface
│   ├── expand.h    # Variable expansion interface
│   ├── builtin.h   # Built-in command interface
│   └── executor.h  # External command execution interface
├── src/
│   ├── token.c     # Token data structure implementation & printing
│   ├── lexer.c     # Lexical scanner implementation with quote/escape handling
│   ├── history.c   # Command history implementation
│   ├── parser.c    # Parser & pipeline construction and printing
│   ├── expand.c    # Environment variable expansion ($NAME / ${NAME})
│   ├── builtin.c   # Built-in commands (cd, pwd, echo, exit) implementation
│   ├── executor.c  # External command execution via fork/execvp/waitpid
│   └── main.c      # Shell REPL, banner, readline loop & built-in execution
├── docs/           # Documentation and design specifications
├── tests/          # Unit tests and validation scripts
└── Makefile        # Project build configuration
```

## Milestone 2 Features Implemented

1. **Tokenization & Data Structures (`token.h` / `token.c`)**:
   - Standardized `token_type_t` enumeration (`TOKEN_WORD`, `TOKEN_PIPE`, `TOKEN_INPUT`, `TOKEN_OUTPUT`, `TOKEN_APPEND`, `TOKEN_BACKGROUND`, `TOKEN_END`).
   - Dynamic token list container (`token_list_t`) with bounds safety.
   - Formatted token printing matching exact assignment requirements (`0 : WORD ls`).

2. **Lexical Analysis (`lexer.h` / `lexer.c`)**:
   - Character scanner processing input into structured tokens.
   - Support for single quotes and double quotes, preserving internal spaces.
   - Support for escape characters (`\`).
   - Operator recognition (`|`, `<`, `>`, `>>`, `&`).
   - Graceful error reporting for unterminated quotes (`Lexer Error : Unterminated ... quote`).

3. **Shell REPL & History (`main.c` / `history.c`)**:
   - GNU Readline integration for prompt input (`shellforge$ `).
   - Welcome banner display.
   - History logging (`add_history` and custom `history` command).
   - Built-in `exit` command handling.

## Milestone 2.2 Features Implemented

1. **Parser (`parser.h` / `parser.c`)**:
   - Converts the token stream into a structured pipeline (`command_t` / `pipeline_t`) of up to 16 commands with up to 128 arguments each.
   - Captures input/output redirection (`<`, `>`, `>>`) and background execution (`&`).
   - Formatted pipeline printing matching exact assignment requirements (`Command 1`, `argv[0] = ls`, aligned `Input` / `Output` / `Append` / `Background` fields).
   - Graceful error reporting for missing redirection filenames and over-long pipelines (`Parser Error : ...`).

2. **Variable Expansion (`expand.h` / `expand.c`)**:
   - Expands environment variables in command arguments after parsing (`$NAME` and `${NAME}`) via `getenv()`.
   - Unset variables expand to the empty string.

3. **REPL Integration (`main.c`)**:
   - Full lex → token print → parse → expand → pipeline print flow on every command.
   - Exact banner and token/pipeline output formats.
   - Ctrl+D exits with `Goodbye!`; built-in `exit` prints `Exiting...`; `history` builtin retained.

## Milestone 3.1 Features Implemented

1. **Built-in Commands (`builtin.h` / `builtin.c`)**:
   - Built-in commands `cd`, `pwd`, `echo`, and `exit` executed by the shell process itself, without `fork()`.
   - `cd` changes the current working directory, defaulting to `HOME` when no argument is given, with graceful error reporting for invalid directories.
   - `pwd` prints the current working directory via `getcwd()`.
   - `echo` prints its arguments joined by single spaces.
   - `exit` terminates the shell through a return code to the main loop, without printing `Exiting...`.

2. **REPL Integration (`main.c`)**:
   - Built-in dispatch runs after the token and pipeline report, only for single-command pipelines (`command_count == 1`).
   - Removed the old `strcmp(line, "exit")` handling; `exit` is now a proper built-in.
   - `history` built-in retained.

## Milestone 3.2 Features Implemented

1. **External Command Execution (`executor.h` / `executor.c`)**:
   - External commands executed via `fork()` + `execvp()` + `waitpid()`.
   - The child process replaces its image with `execvp()`; on failure it reports the error with `perror()` and exits with `exit(1)`.
   - The parent process waits for the child with `waitpid()` and collects its exit status.
   - `fork()` failure is reported with `perror()`.

2. **REPL Integration (`main.c`)**:
   - Built-in commands are still handled in-process; only non-built-in commands go through `fork()`/`execvp()`.
   - External execution runs after the token and pipeline report, only for single-command input (`command_count == 1`).

## Build and Run

To compile the shell:
```bash
make
```

To run the shell:
```bash
./shellforge
# or
make run
```

To clean build artifacts:
```bash
make clean
```