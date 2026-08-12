#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "lexer.h"

int lexer(const char *input, token_list_t *list)
{
    if (input == NULL || list == NULL)
    {
        return -1;
    }

    token_list_init(list);
    int i = 0;
    int len = (int)strlen(input);

    while (i < len)
    {
        // Skip whitespace
        if (isspace((unsigned char)input[i]))
        {
            i++;
            continue;
        }

        // Pipe operator
        if (input[i] == '|')
        {
            token_add(list, TOKEN_PIPE, "|");
            i++;
            continue;
        }

        // Input redirection
        if (input[i] == '<')
        {
            token_add(list, TOKEN_INPUT, "<");
            i++;
            continue;
        }

        // Output redirection / Append redirection
        if (input[i] == '>')
        {
            if (i + 1 < len && input[i + 1] == '>')
            {
                token_add(list, TOKEN_APPEND, ">>");
                i += 2;
            }
            else
            {
                token_add(list, TOKEN_OUTPUT, ">");
                i++;
            }
            continue;
        }

        // Background execution
        if (input[i] == '&')
        {
            token_add(list, TOKEN_BACKGROUND, "&");
            i++;
            continue;
        }

        // Build word token (handling single quotes, double quotes, and escapes)
        char word[MAX_TOKEN_LEN];
        int j = 0;

        while (i < len && !isspace((unsigned char)input[i]))
        {
            // Check for unquoted delimiter tokens breaking a word
            if (input[i] == '|' || input[i] == '<' || input[i] == '>' || input[i] == '&')
            {
                break;
            }

            if (input[i] == '\'')
            {
                i++; // Skip opening single quote
                while (i < len && input[i] != '\'')
                {
                    if (j < MAX_TOKEN_LEN - 1)
                    {
                        word[j++] = input[i];
                    }
                    i++;
                }
                if (i >= len)
                {
                    printf("Lexer Error : Unterminated single quote\n");
                    return -1;
                }
                i++; // Skip closing single quote
            }
            else if (input[i] == '"')
            {
                i++; // Skip opening double quote
                while (i < len && input[i] != '"')
                {
                    if (j < MAX_TOKEN_LEN - 1)
                    {
                        word[j++] = input[i];
                    }
                    i++;
                }
                if (i >= len)
                {
                    printf("Lexer Error : Unterminated double quote\n");
                    return -1;
                }
                i++; // Skip closing double quote
            }
            else if (input[i] == '\\')
            {
                i++; // Skip backslash
                if (i < len)
                {
                    if (j < MAX_TOKEN_LEN - 1)
                    {
                        word[j++] = input[i];
                    }
                    i++;
                }
            }
            else
            {
                if (j < MAX_TOKEN_LEN - 1)
                {
                    word[j++] = input[i];
                }
                i++;
            }
        }

        word[j] = '\0';
        if (j > 0)
        {
            token_add(list, TOKEN_WORD, word);
        }
    }

    token_add(list, TOKEN_END, "END");
    return 0;
}