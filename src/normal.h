#ifndef NORMAL_H
#define NORMAL_H

#include "editor.h"

extern EditorCommand cmd;
extern int is_waiting_for_specifier;

int normal_handle_input(const char *buf, int len);
void normal_register_insertion(char ch);
void normal_insertion_registration_init();
void normal_exec_motion(char ch);
void dispatch_command();

#endif
