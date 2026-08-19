#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "expand.h"

static int is_name_start(char c)
{
    return isalpha((unsigned char)c) || c == '_';
}

static int is_name_char(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

// Grow the buffer so at least `needed` bytes fit
static int grow_buffer(char **buf, size_t *cap, size_t needed)
{
    if (needed <= *cap)
    {
        return 1;
    }

    size_t new_cap = *cap;
    while (new_cap < needed)
    {
        new_cap *= 2;
    }

    char *new_buf = realloc(*buf, new_cap);
    if (new_buf == NULL)
    {
        return 0;
    }

    *buf = new_buf;
    *cap = new_cap;
    return 1;
}

// Append n bytes of text to the buffer, keeping it NUL-terminated
static int append_text(char **buf, size_t *len, size_t *cap, const char *text, size_t n)
{
    if (!grow_buffer(buf, cap, *len + n + 1))
    {
        return 0;
    }

    memcpy(*buf + *len, text, n);
    *len += n;
    (*buf)[*len] = '\0';
    return 1;
}

// Expand $NAME and ${NAME} in a single argument; returns a new malloc'd string
static char *expand_string(const char *arg)
{
    size_t cap = strlen(arg) + 1;
    if (cap < 32)
    {
        cap = 32;
    }

    char *result = malloc(cap);
    if (result == NULL)
    {
        return NULL;
    }

    size_t len = 0;
    result[0] = '\0';

    size_t i = 0;
    while (arg[i] != '\0')
    {
        if (arg[i] != '$')
        {
            if (!append_text(&result, &len, &cap, &arg[i], 1))
            {
                free(result);
                return NULL;
            }
            i++;
            continue;
        }

        // '$' — try to read a variable name after it
        size_t name_start = 0;
        size_t name_len = 0;
        size_t consumed = 0; // characters of arg used by the expansion

        if (arg[i + 1] == '{')
        {
            size_t j = i + 2;
            if (is_name_start(arg[j]))
            {
                name_start = j;
                while (is_name_char(arg[j]))
                {
                    j++;
                }
                if (arg[j] == '}')
                {
                    name_len = j - name_start;
                    consumed = (j + 1) - i;
                }
            }
        }
        else if (is_name_start(arg[i + 1]))
        {
            size_t j = i + 1;
            name_start = j;
            while (is_name_char(arg[j]))
            {
                j++;
            }
            name_len = j - name_start;
            consumed = j - i;
        }

        if (consumed == 0)
        {
            // Not followed by a valid name: leave the '$' untouched
            if (!append_text(&result, &len, &cap, &arg[i], 1))
            {
                free(result);
                return NULL;
            }
            i++;
            continue;
        }

        // getenv needs a NUL-terminated name
        char *name = malloc(name_len + 1);
        if (name == NULL)
        {
            free(result);
            return NULL;
        }
        memcpy(name, arg + name_start, name_len);
        name[name_len] = '\0';

        const char *value = getenv(name);
        free(name);

        // Unset variables expand to the empty string
        if (value != NULL && !append_text(&result, &len, &cap, value, strlen(value)))
        {
            free(result);
            return NULL;
        }

        i += consumed;
    }

    return result;
}

void expand_variables(pipeline_t *pipeline)
{
    if (pipeline == NULL)
    {
        return;
    }

    for (int c = 0; c < pipeline->command_count; c++)
    {
        command_t *cmd = &pipeline->commands[c];

        for (int j = 0; j < cmd->argc; j++)
        {
            if (cmd->argv[j] == NULL)
            {
                continue;
            }

            char *expanded = expand_string(cmd->argv[j]);
            if (expanded == NULL)
            {
                continue; // Keep the original string on allocation failure
            }

            free(cmd->argv[j]);
            cmd->argv[j] = expanded;
        }
    }
}
