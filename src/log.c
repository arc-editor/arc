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

// Thread-safe logging structure
typedef struct {
    FILE *file;
    int line_count;
    pthread_mutex_t mutex;
    int initialized;
} logger_t;

static logger_t g_logger = {NULL, 0, PTHREAD_MUTEX_INITIALIZER, 0};

// Forward declarations
static int ensure_log_directory(void);
static void cleanup_logger(void);
static int get_initial_line_count(const char *filename);
static int rotate_log(void);

// Ensure the log directory exists
static int ensure_log_directory(void) {
    const char *home_dir = getenv("HOME");
    if (!home_dir) {
        fprintf(stderr, "Error: HOME environment variable not set\n");
        return -1;
    }
    
    char cache_dir[512];
    snprintf(cache_dir, sizeof(cache_dir), "%s/.cache", home_dir);
    
    // Create .cache directory if it doesn't exist
    if (mkdir(cache_dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "Error creating cache directory: %s\n", strerror(errno));
        return -1;
    }
    
    char arc_dir[512];
    snprintf(arc_dir, sizeof(arc_dir), "%s/.cache/arc", home_dir);
    
    // Create arc directory if it doesn't exist
    if (mkdir(arc_dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "Error creating arc directory: %s\n", strerror(errno));
        return -1;
    }
    
    return 0;
}

// Get full path to log file
static void get_log_path(char *path, size_t path_size, const char *name) {
    const char *home_dir = getenv("HOME");
    if (!home_dir) {
        fprintf(stderr, "Error: HOME environment variable not set\n");
        exit(1);
    }
    snprintf(path, path_size, "%s/.cache/arc/%s", home_dir, name);
}

// Count lines in file, handling files that don't end with newline
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

// Rotate log file when it gets too large
static int rotate_log(void) {
    if (!g_logger.file) {
        return -1;
    }
    
    fclose(g_logger.file);
    g_logger.file = NULL;

    char log_filename[512];
    char tmp_log_filename[512];
    get_log_path(log_filename, sizeof(log_filename), "arc.log");
    get_log_path(tmp_log_filename, sizeof(tmp_log_filename), "arc_tmp.log");
    
    FILE *old_file = fopen(log_filename, "r");
    if (!old_file) {
        // If we can't read the old file, just create a new one
        g_logger.file = fopen(log_filename, "w");
        g_logger.line_count = 0;
        return g_logger.file ? 0 : -1;
    }
    
    FILE *temp_file = fopen(tmp_log_filename, "w");
    if (!temp_file) {
        fclose(old_file);
        // Fallback: reopen in append mode
        g_logger.file = fopen(log_filename, "a");
        return g_logger.file ? 0 : -1;
    }
    
    // Skip the first (line_count - TRUNCATE_TO_LINES) lines
    int skip_lines = g_logger.line_count - TRUNCATE_TO_LINES;
    char buffer[2048];
    
    for (int i = 0; i < skip_lines && fgets(buffer, sizeof(buffer), old_file); i++) {
        // Just skip these lines
    }
    
    // Copy the remaining lines to temp file
    while (fgets(buffer, sizeof(buffer), old_file)) {
        fputs(buffer, temp_file);
    }
    
    fclose(old_file);
    fclose(temp_file);
    
    // Replace original file with temp file
    if (remove(log_filename) != 0) {
        fprintf(stderr, "Warning: Could not remove old log file: %s\n", strerror(errno));
    }
    
    if (rename(tmp_log_filename, log_filename) != 0) {
        fprintf(stderr, "Error: Log rotation rename failed: %s\n", strerror(errno));
        // Try to clean up temp file
        remove(tmp_log_filename);
        return -1;
    }
    
    // Reopen the file for appending
    g_logger.file = fopen(log_filename, "a");
    if (!g_logger.file) {
        fprintf(stderr, "Error: Unable to reopen log file after rotation: %s\n", strerror(errno));
        return -1;
    }
    
    // Update line count after rotation
    g_logger.line_count = TRUNCATE_TO_LINES;
    return 0;
}

// Initialize the logger
static int init_logger(void) {
    if (g_logger.initialized) {
        return 0;
    }
    
    if (ensure_log_directory() != 0) {
        return -1;
    }
    
    char log_filename[512];
    get_log_path(log_filename, sizeof(log_filename), "micro.log");
    
    g_logger.file = fopen(log_filename, "a");
    if (!g_logger.file) {
        fprintf(stderr, "Error: Unable to open log file: %s\n", strerror(errno));
        return -1;
    }
    
    g_logger.line_count = get_initial_line_count(log_filename);
    g_logger.initialized = 1;
    
    // Register cleanup function
    atexit(cleanup_logger);
    
    return 0;
}

// Cleanup function called on exit
static void cleanup_logger(void) {
    pthread_mutex_lock(&g_logger.mutex);
    if (g_logger.file) {
        fclose(g_logger.file);
        g_logger.file = NULL;
    }
    g_logger.initialized = 0;
    pthread_mutex_unlock(&g_logger.mutex);
}

// Main logging function - thread-safe
void log_write(const char *fmt, ...) {
    pthread_mutex_lock(&g_logger.mutex);
    
    // Initialize logger if needed
    if (!g_logger.initialized) {
        if (init_logger() != 0) {
            pthread_mutex_unlock(&g_logger.mutex);
            return;
        }
    }
    
    // Check if we need to rotate the log
    if (g_logger.line_count >= MAX_LOG_LINES) {
        if (rotate_log() != 0) {
            fprintf(stderr, "Warning: Log rotation failed\n");
            // Continue logging to current file
        }
    }
    
    // Get current time with millisecond precision
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm *tm_info = localtime(&tv.tv_sec);
    
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    // Write timestamp
    fprintf(g_logger.file, "[%s.%03ld] ", time_str, tv.tv_usec / 1000);
    
    // Write the formatted message
    va_list args;
    va_start(args, fmt);
    vfprintf(g_logger.file, fmt, args);
    va_end(args);
    
    fputc('\n', g_logger.file);
    fflush(g_logger.file);
    g_logger.line_count++;
    
    pthread_mutex_unlock(&g_logger.mutex);
}

// Convenience logging functions with proper buffer management
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
        // Message was truncated
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
        // Message was truncated
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
        // Message was truncated
        strcpy(buffer + MAX_LOG_MESSAGE - 15, "... [TRUNCATED]");
    }
    
    log_write("[ERROR] %s", buffer);
    free(buffer);
}
