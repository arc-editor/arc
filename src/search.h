#ifndef SEARCH_H
#define SEARCH_H

void search_init(int direction);
int search_handle_input(const char *buf, int len);
const char *search_get_term(void);
char search_get_prompt_char(void);
const char *search_get_last_term(void);
int search_get_last_direction(void);

#endif
