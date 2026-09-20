#!/usr/bin/env bash
#
# Extended functional tests for ShellForge background execution,
# SIGCHLD handling, PID reporting, and /dev/null stdin.
#

set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
cd "$PROJECT_ROOT" || { echo "cannot cd to $PROJECT_ROOT" >&2; exit 1; }

WORK=$(mktemp -d /tmp/shellforge-ext-tests.XXXXXX)

cleanup()
{
    rm -rf "$WORK"
}
trap cleanup EXIT
trap 'exit 1' INT TERM

if [ ! -x ./shellforge ]
then
    echo "SKIP: ./shellforge not found" >&2
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

run_shell()
{
    {
        printf '%s\n' "$@"
        printf 'exit\n'
    } | ./shellforge 2>&1
}

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

# --- Background PID Reporting ---

# 11. Background single command: PID is reported
OUT=$(run_shell 'sleep 0.1 &')
expect_match bg-pid-report "$OUT" '^\[Background PID: [0-9]+\]$'

# 12. Background pipeline: first PID is reported
OUT=$(run_shell 'sleep 0.1 | cat &')
expect_match bg-pipe-pid-report "$OUT" '^\[Background PID: [0-9]+\]$'

# --- Background Non-Blocking ---

# 13. Background does not block: shell returns immediately
OUT_FILE="$WORK/bg_time.out"
START=$(date +%s)
{
    printf 'sleep 5 &\necho bg-nonblock\nexit\n'
} | ./shellforge > "$OUT_FILE" 2>&1
END=$(date +%s)
ELAPSED=$((END - START))
OUT=$(cat "$OUT_FILE")

expect_line bg-nonblock "$OUT" 'bg-nonblock'
if [ "$ELAPSED" -lt 3 ]
then
    pass bg-nonblock-time
else
    fail "bg-nonblock-time (took ${ELAPSED}s)"
fi

# 14. Background pipeline does not block
PIPE_OUT_FILE="$WORK/pipe_bg_time.out"
START=$(date +%s)
{
    printf 'sleep 5 | cat &\necho pipe-bg-nonblock\nexit\n'
} | ./shellforge > "$PIPE_OUT_FILE" 2>&1
END=$(date +%s)
ELAPSED=$((END - START))
OUT=$(cat "$PIPE_OUT_FILE")

expect_line pipe-bg-nonblock "$OUT" 'pipe-bg-nonblock'
if [ "$ELAPSED" -lt 3 ]
then
    pass pipe-bg-nonblock-time
else
    fail "pipe-bg-nonblock-time (took ${ELAPSED}s)"
fi

# --- Background stdin /dev/null ---

# 15. cat & should not hang (stdin redirected to /dev/null)
TIMEOUT_RESULT=0
timeout 3 bash -c '{
    printf "cat &\nexit\n"
} | '"$PROJECT_ROOT"'/shellforge 2>&1' > /dev/null || TIMEOUT_RESULT=$?

if [ "$TIMEOUT_RESULT" -ne 124 ]
then
    pass bg-stdin-devnull
else
    fail "bg-stdin-devnull (cat & hung)"
fi

# --- Background + Redirection ---

# 16. Background + output redirection
OUT=$(run_shell "echo bg-redir > $WORK/bg_out.txt &")
sleep 0.3
expect_file bg-output-redir "$WORK/bg_out.txt" 'bg-redir'

# 17. Background + append redirection
OUT=$(run_shell "echo bg-app-1 >> $WORK/bg_app.txt &")
sleep 0.2
OUT=$(run_shell "echo bg-app-2 >> $WORK/bg_app.txt &")
sleep 0.2
EXPECTED=$(printf 'bg-app-1\nbg-app-2')
if [ -f "$WORK/bg_app.txt" ] && [ "$(cat "$WORK/bg_app.txt")" = "$EXPECTED" ]
then
    pass bg-append-redir
else
    fail bg-append-redir
    [ -f "$WORK/bg_app.txt" ] && sed 's/^/    | /' "$WORK/bg_app.txt"
fi

# --- Background + Input Redirection ---

# 18. Explicit input redirection overrides /dev/null
echo "bg-input-content" > "$WORK/bg_input.txt"
OUT=$(run_shell "cat < $WORK/bg_input.txt &")
sleep 0.3
expect_match bg-input-redir "$OUT" 'bg-input-content'

# --- Short-lived Background Jobs (zombie check) ---

# 19. Multiple short-lived background jobs don't leave zombies
OUT=$(run_shell 'true &' 'true &' 'true &' 'true &' 'true &')
sleep 0.5
# Check for zombie children of this test process (rough check)
ZOMBIES=$(ps -eo state,ppid 2>/dev/null | grep -c "^Z.*$$" || true)
if [ "$ZOMBIES" -eq 0 ]
then
    pass no-zombies
else
    fail "no-zombies ($ZOMBIES zombie(s))"
fi

# --- Error handling ---

# 20. Nonexistent background command reports error, doesn't crash
OUT=$(run_shell 'definitely_not_a_real_command_xyz &' 'echo error-ok')
expect_match bg-exec-error "$OUT" '([Nn]o such file|execvp)'
expect_line bg-after-error "$OUT" 'error-ok'

# --- Pipeline + Redirection ---

# 21. Pipeline plus output redirection
OUT=$(run_shell "echo pipe-redir | cat > $WORK/pipe_out.txt")
expect_file pipe-redir "$WORK/pipe_out.txt" 'pipe-redir'

# 22. Pipeline + background + redirection
OUT=$(run_shell "echo pipe-bg-redir | cat > $WORK/pipe_bg_out.txt &")
sleep 0.3
expect_file pipe-bg-redir "$WORK/pipe_bg_out.txt" 'pipe-bg-redir'

# --- Builtins preserved ---

# 23. cd + pwd still work
OUT=$(run_shell 'cd /tmp' 'pwd')
expect_line builtin-cd-pwd "$OUT" '/tmp'

# 24. history still works
OUT=$(run_shell 'echo test-hist' 'history')
expect_match builtin-history "$OUT" 'echo test-hist'

# --- Summary ---

echo '----------------------------------------'
echo "Extended Summary: $PASS_COUNT passed, $FAIL_COUNT failed"

if [ "$FAIL_COUNT" -ne 0 ]
then
    exit 1
fi
exit 0
