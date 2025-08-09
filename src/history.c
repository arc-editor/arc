#include <stdlib.h>
#include <string.h>
#include "history.h"
#include "log.h"

static Change* change_create(ChangeType type, int y, int x, const char *text) {
    Change *change = malloc(sizeof(Change));
    if (!change) {
        log_error("Failed to allocate memory for Change");
        return NULL;
    }
    change->type = type;
    change->y = y;
    change->x = x;
    change->text = strdup(text);
    if (!change->text) {
        log_error("Failed to duplicate text for Change");
        free(change);
        return NULL;
    }
    change->next = NULL;
    change->prev = NULL;
    return change;
}

static void change_destroy(Change *change) {
    if (change) {
        free(change->text);
        free(change);
    }
}

static void changestack_init(ChangeStack *stack) {
    stack->head = NULL;
    stack->tail = NULL;
    stack->count = 0;
}

static void changestack_clear(ChangeStack *stack) {
    Change *current = stack->head;
    while (current) {
        Change *next = current->next;
        change_destroy(current);
        current = next;
    }
    changestack_init(stack);
}

static void changestack_push(ChangeStack *stack, Change *change) {
    if (stack->head == NULL) {
        stack->head = change;
        stack->tail = change;
    } else {
        stack->head->prev = change;
        change->next = stack->head;
        stack->head = change;
    }
    stack->count++;
}

static Change* changestack_pop(ChangeStack *stack) {
    if (stack->head == NULL) {
        return NULL;
    }
    Change *change = stack->head;
    stack->head = stack->head->next;
    if (stack->head) {
        stack->head->prev = NULL;
    } else {
        stack->tail = NULL;
    }
    stack->count--;
    change->next = NULL;
    change->prev = NULL;
    return change;
}

History* history_create(void) {
    History *h = malloc(sizeof(History));
    if (!h) {
        log_error("Failed to allocate memory for History");
        return NULL;
    }
    changestack_init(&h->undo_stack);
    changestack_init(&h->redo_stack);
    h->is_coalescing = 0;
    return h;
}

void history_destroy(History *h) {
    if (h) {
        changestack_clear(&h->undo_stack);
        changestack_clear(&h->redo_stack);
        free(h);
    }
}

void history_add_change(History *h, ChangeType type, int y, int x, const char *text) {
    if (h == NULL) return;

    Change *change = change_create(type, y, x, text);
    if (!change) return;

    changestack_push(&h->undo_stack, change);
    history_clear_redo(h);
}

Change* history_pop_undo(History *h) {
    return changestack_pop(&h->undo_stack);
}

void history_push_undo(History *h, Change *change) {
    changestack_push(&h->undo_stack, change);
}

Change* history_pop_redo(History *h) {
    return changestack_pop(&h->redo_stack);
}

void history_push_redo(History *h, Change *change) {
    changestack_push(&h->redo_stack, change);
}

void history_clear_redo(History *h) {
    changestack_clear(&h->redo_stack);
}

void history_start_coalescing(History *h) {
    if (h) {
        h->is_coalescing = 1;
    }
}

void history_end_coalescing(History *h) {
    if (h) {
        h->is_coalescing = 0;
    }
}
