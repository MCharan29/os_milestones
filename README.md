# Shellforge - Unix-Style Shell in C

Shellforge is a lightweight, modular Unix-style command-line shell implemented in C for systems programming and operating systems course projects.

## Project Structure

```
shellforge/
├── include/
│   ├── token.h     # Token definitions, token types, and list declarations
│   ├── lexer.h     # Lexical analyzer interface
│   └── history.h   # In-memory history tracking interface
├── src/
│   ├── token.c     # Token data structure implementation & printing
│   ├── lexer.c     # Lexical scanner implementation with quote/escape handling
│   ├── history.c   # Command history implementation
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