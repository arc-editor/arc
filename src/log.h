#ifndef LOG_H
#define LOG_H

#include <stddef.h>

void log_info(const char *fmt, ...);
void log_warning(const char *fmt, ...);
void log_error(const char *fmt, ...);
void get_log_path(char *path, size_t path_size, const char *name);

#endif
