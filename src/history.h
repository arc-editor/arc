#ifndef HISTORY_H
#define HISTORY_H

typedef enum {
    CHANGE_TYPE_INSERT,
    CHANGE_TYPE_DELETE,
} ChangeType;

// Represents a single atomic change to the buffer
typedef struct Change {
    ChangeType type;
    int y, x; // Position of the change
    char *text;
    struct Change *next;
    struct Change *prev;
} Change;

// A stack of changes
typedef struct {
    Change *head;
    Change *tail;
    int count;
} ChangeStack;

// Main struct for history management
typedef struct {
    ChangeStack undo_stack;
    ChangeStack redo_stack;
    int is_coalescing;
} History;

History* history_create(void);
void history_destroy(History *h);

void history_add_change(History *h, ChangeType type, int y, int x, const char *text);

Change* history_pop_undo(History *h);
void history_push_undo(History *h, Change *change);

Change* history_pop_redo(History *h);
void history_push_redo(History *h, Change *change);

void history_clear_redo(History *h);

void history_start_coalescing(History *h);
void history_end_coalescing(History *h);

#endif // HISTORY_H
