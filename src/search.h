#ifndef SEARCH_H
#define SEARCH_H

void search_init(int direction);
int search_handle_input(const char *ch);
const char *search_get_term(void);
char search_get_prompt_char(void);

#endif
