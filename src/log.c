#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

#define MAX_LOG_LINES 10000
#define TRUNCATE_TO_LINES 5000
#define MAX_LOG_MESSAGE 4096


static int foo = 3;

typedef struct {
    FILE *file;
    int line_count;
    pthread_mutex_t mutex;
    int initialized;
} Logger;

static Logger logger = {
    .file = NULL,
    .line_count = 0,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .initialized = 0,
};

static int ensure_log_directory(void) {
    const char *home_dir = getenv("HOME");
    if (!home_dir) {
        fprintf(stderr, "Error: HOME environment variable not set\n");
        return -1;
    }
    
    char cache_dir[512];
    snprintf(cache_dir, sizeof(cache_dir), "%s/.cache", home_dir);
    
    if (mkdir(cache_dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "Error creating cache directory: %s\n", strerror(errno));
        return -1;
    }

    char arc_dir[512];
    snprintf(arc_dir, sizeof(arc_dir), "%s/.cache/arc", home_dir);
    if (mkdir(arc_dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "Error creating arc directory: %s\n", strerror(errno));
        return -1;
    }
    
    return 0;
}

static void get_log_path(char *path, size_t path_size, const char *name) {
    const char *home_dir = getenv("HOME");
    if (!home_dir) {
        fprintf(stderr, "Error: HOME environment variable not set\n");
        exit(1);
    }
    snprintf(path, path_size, "%s/.cache/arc/%s", home_dir, name);
}

static int get_initial_line_count(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return 0;

    int lines = 0;
    int ch;
    int last_char = '\n'; // Assume file starts after a newline
    
    while ((ch = fgetc(f)) != EOF) {
        if (ch == '\n') {
            lines++;
        }
        last_char = ch;
    }
    
    // If file doesn't end with newline, we'll be adding one, so count it
    if (last_char != '\n' && last_char != EOF) {
        lines++;
    }
    
    fclose(f);
    return lines;
}

static int rotate_log(void) {
    if (!logger.file) {
        return -1;
    }
    
    fclose(logger.file);
    logger.file = NULL;

    char log_filename[512];
    char tmp_log_filename[512];
    get_log_path(log_filename, sizeof(log_filename), "arc.log");
    get_log_path(tmp_log_filename, sizeof(tmp_log_filename), "arc_tmp.log");
    
    FILE *old_file = fopen(log_filename, "r");
    if (!old_file) {
        logger.file = fopen(log_filename, "w");
        logger.line_count = 0;
        return logger.file ? 0 : -1;
    }
    
    FILE *temp_file = fopen(tmp_log_filename, "w");
    if (!temp_file) {
        fclose(old_file);
        logger.file = fopen(log_filename, "a");
        return logger.file ? 0 : -1;
    }
    
    int skip_lines = logger.line_count - TRUNCATE_TO_LINES;
    char buffer[2048];
    
    for (int i = 0; i < skip_lines && fgets(buffer, sizeof(buffer), old_file); i++) {
        // Just skip these lines
    }
    
    while (fgets(buffer, sizeof(buffer), old_file)) {
        fputs(buffer, temp_file);
    }
    
    fclose(old_file);
    fclose(temp_file);
    
    if (remove(log_filename) != 0) {
        fprintf(stderr, "Warning: Could not remove old log file: %s\n", strerror(errno));
    }
    
    if (rename(tmp_log_filename, log_filename) != 0) {
        fprintf(stderr, "Error: Log rotation rename failed: %s\n", strerror(errno));
        remove(tmp_log_filename);
        return -1;
    }
    
    logger.file = fopen(log_filename, "a");
    if (!logger.file) {
        fprintf(stderr, "Error: Unable to reopen log file after rotation: %s\n", strerror(errno));
        return -1;
    }
    
    logger.line_count = TRUNCATE_TO_LINES;
    return 0;
}


static void cleanup_logger(void) {
    pthread_mutex_lock(&logger.mutex);
    if (logger.file) {
        fclose(logger.file);
        logger.file = NULL;
    }
    logger.initialized = 0;
    pthread_mutex_unlock(&logger.mutex);
}

static int init_logger(void) {
    if (logger.initialized) {
        return 0;
    }
    
    if (ensure_log_directory() != 0) {
        return -1;
    }
    
    char log_filename[512];
    get_log_path(log_filename, sizeof(log_filename), "arc.log");
    
    logger.file = fopen(log_filename, "a");
    if (!logger.file) {
        fprintf(stderr, "Error: Unable to open log file: %s\n", strerror(errno));
        return -1;
    }
    
    logger.line_count = get_initial_line_count(log_filename);
    logger.initialized = 1;
    
    // Register cleanup function
    atexit(cleanup_logger);
    
    return 0;
}

void log_write(const char *fmt, ...) {
    pthread_mutex_lock(&logger.mutex);
    
    if (!logger.initialized) {
        if (init_logger() != 0) {
            pthread_mutex_unlock(&logger.mutex);
            return;
        }
    }
    
    if (logger.line_count >= MAX_LOG_LINES) {
        if (rotate_log() != 0) {
            fprintf(stderr, "Warning: Log rotation failed\n");
        }
    }
    
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm *tm_info = localtime(&tv.tv_sec);
    
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    fprintf(logger.file, "[%s.%03ld] ", time_str, tv.tv_usec / 1000);
    
    va_list args;
    va_start(args, fmt);
    vfprintf(logger.file, fmt, args);
    va_end(args);
    
    fputc('\n', logger.file);
    fflush(logger.file);
    logger.line_count++;
    
    pthread_mutex_unlock(&logger.mutex);
}

void log_info(const char *fmt, ...) {
    char *buffer = malloc(MAX_LOG_MESSAGE);
    if (!buffer) {
        log_write("[INFO] [Memory allocation failed for log message]");
        return;
    }
    
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(buffer, MAX_LOG_MESSAGE, fmt, args);
    va_end(args);
    
    if (ret >= MAX_LOG_MESSAGE) {
        strcpy(buffer + MAX_LOG_MESSAGE - 15, "... [TRUNCATED]");
    }
    
    log_write("[INFO] %s", buffer);
    free(buffer);
}

void log_warning(const char *fmt, ...) {
    char *buffer = malloc(MAX_LOG_MESSAGE);
    if (!buffer) {
        log_write("[WARNING] [Memory allocation failed for log message]");
        return;
    }
    
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(buffer, MAX_LOG_MESSAGE, fmt, args);
    va_end(args);
    
    if (ret >= MAX_LOG_MESSAGE) {
        strcpy(buffer + MAX_LOG_MESSAGE - 15, "... [TRUNCATED]");
    }
    
    log_write("[WARNING] %s", buffer);
    free(buffer);
}

void log_error(const char *fmt, ...) {
    char *buffer = malloc(MAX_LOG_MESSAGE);
    if (!buffer) {
        log_write("[ERROR] [Memory allocation failed for log message]");
        return;
    }
    
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(buffer, MAX_LOG_MESSAGE, fmt, args);
    va_end(args);
    
    if (ret >= MAX_LOG_MESSAGE) {
        strcpy(buffer + MAX_LOG_MESSAGE - 15, "... [TRUNCATED]");
    }
    
    log_write("[ERROR] %s", buffer);
    free(buffer);
}
