#include <stdio.h>
#include <string.h>
#include "token.h"

void token_list_init(token_list_t *list)
{
    if (list != NULL)
    {
        list->count = 0;
    }
}

void token_add(token_list_t *list, token_type_t type, const char *text)
{
    if (list == NULL || list->count >= MAX_TOKENS)
    {
        return;
    }

    token_t *t = &list->tokens[list->count];
    t->type = type;
    if (text != NULL)
    {
        strncpy(t->text, text, MAX_TOKEN_LEN - 1);
        t->text[MAX_TOKEN_LEN - 1] = '\0';
    }
    else
    {
        t->text[0] = '\0';
    }
    list->count++;
}

const char* token_name(token_type_t type)
{
    switch (type)
    {
        case TOKEN_WORD:       return "WORD";
        case TOKEN_PIPE:       return "PIPE";
        case TOKEN_INPUT:      return "INPUT";
        case TOKEN_OUTPUT:     return "OUTPUT";
        case TOKEN_APPEND:     return "APPEND";
        case TOKEN_BACKGROUND: return "BACKGROUND";
        case TOKEN_END:        return "END";
        default:               return "UNKNOWN";
    }
}

void token_print(const token_list_t *list)
{
    if (list == NULL)
    {
        return;
    }

    printf("\n------------- TOKENS -------------\n");
    for (int i = 0; i < list->count; i++)
    {
        printf(" %d : %-13s%s\n", i, token_name(list->tokens[i].type), list->tokens[i].text);
    }
    printf("----------------------------------\n");
}