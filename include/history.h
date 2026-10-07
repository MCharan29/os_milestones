#ifndef HISTORY_H
#define HISTORY_H

#define MAX_HISTORY_ENTRIES 100

void history_add(const char *cmd);
void history_print(void);
void history_free(void);

#endif /* HISTORY_H */
