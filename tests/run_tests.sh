#!/usr/bin/env bash
#
# tests/run_tests.sh - end-to-end tests for shellforge process creation and
# pipeline execution:
#   Process Creation & External Command Execution (fork/execvp/waitpid),
#     background (&), and execvp error reporting: tests 1, 8, 10.
#   Pipeline Execution & File Descriptor Management (pipe/dup2) and
#     I/O redirection (<, >, >> via open/dup2): tests 2-7.
#   Built-in Command Handling (in-process builtins): test 9.
#
# Each test feeds command lines into ./shellforge on stdin, captures the
# combined stdout+stderr, and greps for distinctive command-output lines.
# The shell echoes a TOKENS report and a PIPELINE report for every command
# line, so assertions use exact whole-line matches that those reports can
# never contain (report lines are always prefixed, e.g. "argv[1] = /tmp").

set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
cd "$PROJECT_ROOT" || { echo "cannot cd to $PROJECT_ROOT" >&2; exit 1; }

WORK=$(mktemp -d /tmp/shellforge-tests.XXXXXX)

cleanup()
{
    rm -rf "$WORK"
}
trap cleanup EXIT
trap 'exit 1' INT TERM

# Build first; fail loudly if the build is broken.
if ! make >"$WORK/build.log" 2>&1
then
    echo "BUILD FAILED: make did not succeed" >&2
    cat "$WORK/build.log" >&2
    exit 1
fi

if [ ! -x ./shellforge ]
then
    echo "BUILD FAILED: ./shellforge was not produced" >&2
    exit 1
fi

PASS_COUNT=0
FAIL_COUNT=0

pass()
{
    echo "PASS $1"
    PASS_COUNT=$((PASS_COUNT + 1))
}

fail()
{
    echo "FAIL $1"
    FAIL_COUNT=$((FAIL_COUNT + 1))
}

# Run the shell with the given command lines (an "exit" line is appended
# automatically) and print the combined stdout+stderr.
run_shell()
{
    {
        printf '%s\n' "$@"
        printf 'exit\n'
    } | ./shellforge 2>&1
}

# Assert that captured output $2 contains the exact whole line $3.
expect_line()
{
    if printf '%s\n' "$2" | grep -qxF -- "$3"
    then
        pass "$1"
    else
        fail "$1"
        printf '%s\n' "$2" | sed 's/^/    | /'
    fi
}

# Assert that captured output $2 has a line matching extended regex $3.
expect_match()
{
    if printf '%s\n' "$2" | grep -qE -- "$3"
    then
        pass "$1"
    else
        fail "$1"
        printf '%s\n' "$2" | sed 's/^/    | /'
    fi
}

# Assert that file $2 exists and contains exactly the single line $3.
expect_file()
{
    if [ -f "$2" ] && [ "$(cat "$2")" = "$3" ]
    then
        pass "$1"
    else
        fail "$1"
        if [ -f "$2" ]
        then
            sed 's/^/    | /' "$2"
        else
            echo "    | (file $2 missing)"
        fi
    fi
}

# --- Process Creation: external command execution (fork/execvp/waitpid) ---

# 1. Single external command via fork/execvp/waitpid.
OUT=$(run_shell '/bin/echo sf-basic-ok')
expect_line single-external "$OUT" 'sf-basic-ok'

# --- Pipeline Execution & I/O Redirection: pipes (pipe/dup2) & redirection ---

# 2. Output redirection: echo's output goes into the file, not the terminal.
OUT=$(run_shell "echo sf-redir > $WORK/out.txt")
expect_file redirect-output "$WORK/out.txt" 'sf-redir'

# 3. Append redirection: two >> commands keep both lines, in order.
OUT=$(run_shell "echo sf-app-1 >> $WORK/app.txt" "echo sf-app-2 >> $WORK/app.txt")
EXPECTED=$(printf 'sf-app-1\nsf-app-2')
if [ -f "$WORK/app.txt" ] && [ "$(cat "$WORK/app.txt")" = "$EXPECTED" ]
then
    pass redirect-append
else
    fail redirect-append
    [ -f "$WORK/app.txt" ] && sed 's/^/    | /' "$WORK/app.txt"
fi

# 4. Input redirection: cat reads from the file created by test 2.
OUT=$(run_shell "cat < $WORK/out.txt")
expect_line redirect-input "$OUT" 'sf-redir'

# 5. Two-stage pipe: echo | wc -w counts the words.
OUT=$(run_shell 'echo hello world | wc -w')
expect_match pipe-two-stage "$OUT" '^ *2 *$'

# 6. Three-stage pipe: echo | tr | grep | wc -l -> lines "one" and "two".
OUT=$(run_shell "echo one two three | tr ' ' '\n' | grep o | wc -l")
expect_match pipe-three-stage "$OUT" '^ *2 *$'

# 7. Pipe plus redirection: the explicit > file receives the pipe's output.
OUT=$(run_shell "echo sf-piped | cat > $WORK/p.txt")
expect_file pipe-plus-redirect "$WORK/p.txt" 'sf-piped'

# --- Background Execution: asynchronous commands with sleep ---

# 8. Background: sleep runs with &, and the shell stays responsive.
OUT=$(run_shell 'sleep 0.2 &' 'echo bg-ok')
expect_line background "$OUT" 'bg-ok'

# --- Built-in Command Handling: in-process builtins ---

# 9. Builtins run in-process: cd changes what the next pwd prints.
OUT=$(run_shell 'cd /tmp' 'pwd')
expect_line builtin-cd-pwd "$OUT" '/tmp'

# --- Process Management: execvp error reporting ---

# 10. execvp failure reports an error instead of hanging or crashing.
OUT=$(run_shell 'definitely_not_a_real_command_xyz')
expect_match exec-error "$OUT" '([Nn]o such file|execvp)'

echo '----------------------------------------'
echo "Summary: $PASS_COUNT passed, $FAIL_COUNT failed"

if [ "$FAIL_COUNT" -ne 0 ]
then
    exit 1
fi
exit 0
