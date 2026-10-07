#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "history.h"

static char *history_list[MAX_HISTORY_ENTRIES];
static int history_count = 0;

void history_add(const char *cmd)
{
    if (cmd == NULL || strlen(cmd) == 0)
    {
        return;
    }

    if (history_count < MAX_HISTORY_ENTRIES)
    {
        history_list[history_count] = strdup(cmd);
        if (history_list[history_count] != NULL)
        {
            history_count++;
        }
    }
    else
    {
        free(history_list[0]);
        for (int i = 1; i < MAX_HISTORY_ENTRIES; i++)
        {
            history_list[i - 1] = history_list[i];
        }
        history_list[MAX_HISTORY_ENTRIES - 1] = strdup(cmd);
    }
}

void history_print(void)
{
    printf("------ Command History ------\n");
    for (int i = 0; i < history_count; i++)
    {
        printf("%d %s\n", i + 1, history_list[i]);
    }
}

void history_free(void)
{
    for (int i = 0; i < history_count; i++)
    {
        if (history_list[i] != NULL)
        {
            free(history_list[i]);
            history_list[i] = NULL;
        }
    }
    history_count = 0;
}
