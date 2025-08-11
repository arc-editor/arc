#include <stdlib.h>
#include <string.h>
#include "buffer.h"
#include "buffer_lines.h"

const int MAX_CHILDREN = 256;
const int MIN_CHILDREN = MAX_CHILDREN / 2;

static BufferLinesNode* insert_recursive(BufferLinesNode *node, BufferLine *line, int position);
static void delete_recursive(BufferLinesNode *node, int position);
static void rebalance_after_delete(BufferLinesNode *parent, int child_index_with_underflow);

BufferLinesNode *buffer_lines_new_leaf_node(int offset) {
  BufferLinesNode *ptr = malloc(sizeof(BufferLinesNode));
  if (ptr == NULL) return NULL;
  ptr->lines = malloc(sizeof(BufferLine *) * MAX_CHILDREN);
  if (ptr->lines == NULL) {
    free(ptr);
    return NULL;
  }
  ptr->offset = offset;
  ptr->line_count = 0;
  ptr->children_count = 0;
  ptr->children = NULL;
  return ptr;
}

BufferLinesNode *buffer_lines_new_internal_node(int offset) {
  BufferLinesNode *ptr = malloc(sizeof(BufferLinesNode));
  if (ptr == NULL) return NULL;
  ptr->lines = NULL;
  ptr->offset = offset;
  ptr->line_count = 0;
  ptr->children_count = 0;
  ptr->children = malloc(sizeof(BufferLinesNode *) * MAX_CHILDREN);
  if (ptr->children == NULL) {
    free(ptr);
    return NULL;
  }
  return ptr;
}

/**
 * Recursively frees the entire tree structure.
 * It frees all nodes and their internal arrays (lines or children).
 */
void buffer_lines_free_node(BufferLinesNode *root) {
    if (root == NULL) {
        return;
    }

    // If it's an internal node, free its children first
    if (root->children != NULL) {
        for (int i = 0; i < root->children_count; i++) {
            buffer_lines_free_node(root->children[i]);
        }
        free(root->children);
    }

    // If it's a leaf node, free its lines array
    if (root->lines != NULL) {
        for (int i = 0; i < root->line_count; i++) {
          free(root->lines[i]);
        }
        free(root->lines);
    }

    // Finally, free the node struct itself
    free(root);
}

/**
 * Returns the total number of lines in the buffer.
 * @param root The root node of the buffer lines tree.
 * @return The total line count.
 */
int buffer_lines_count(BufferLinesNode *root) {
    if (root == NULL) {
        return 0;
    }
    return root->line_count;
}

/**
 * Retrieves a pointer to the BufferLine at a given position.
 * @param root The root node of the buffer lines tree.
 * @param position The zero-based line number to retrieve.
 * @return A pointer to the BufferLine, or NULL if the position is out of bounds.
 */
BufferLine *buffer_lines_get(BufferLinesNode *root, int position) {
    if (!root || position < 0 || position >= root->line_count) {
        return NULL;
    }

    BufferLinesNode *current = root;
    while (current->children != NULL) { // While it's an internal node
        int child_index = 0;
        for (child_index = 0; child_index < current->children_count; ++child_index) {
            BufferLinesNode *child = current->children[child_index];
            if (position < child->line_count) {
                current = child; // Descend into this child
                break;
            }
            position -= child->line_count; // Adjust position for next sibling
        }
    }

    // Now 'current' is a leaf node
    if (position < current->line_count) {
        return current->lines[position];
    }

    return NULL; // Should be unreachable if initial checks are correct
}

/**
 * Inserts a line into the tree at a given position.
 * Handles root creation and splitting.
 * @param root_ptr A pointer to the root node pointer.
 * @param line The BufferLine to insert.
 * @param position The zero-based line number for the insertion.
 */
void buffer_lines_insert_line(BufferLinesNode **root_ptr, BufferLine *line, int position) {
    if (*root_ptr == NULL) {
        *root_ptr = buffer_lines_new_leaf_node(0);
    }

    BufferLinesNode *new_sibling = insert_recursive(*root_ptr, line, position);

    // If the recursive call split the original root, create a new root.
    if (new_sibling) {
        BufferLinesNode *new_root = buffer_lines_new_internal_node(0);
        new_root->children[0] = *root_ptr;
        new_root->children[1] = new_sibling;
        new_root->children_count = 2;
        new_root->line_count = (*root_ptr)->line_count + new_sibling->line_count;
        *root_ptr = new_root;
    }
}

/**
 * Recursive helper for insertion.
 * Returns a new sibling node if `node` splits, otherwise returns NULL.
 */
static BufferLinesNode* insert_recursive(BufferLinesNode *node, BufferLine *line, int position) {
    // Case 1: We are at a LEAF node.
    if (node->children == NULL) {
        if (position > node->line_count) position = node->line_count;

        // Shift lines to make space for the new one.
        memmove(&node->lines[position + 1], &node->lines[position], (node->line_count - position) * sizeof(BufferLine*));
        node->lines[position] = line;
        node->line_count++;

        // If the node is not full, we're done.
        if (node->line_count < MAX_CHILDREN) {
            return NULL;
        }

        // Otherwise, SPLIT the leaf node.
        int split_at = MAX_CHILDREN / 2;
        BufferLinesNode *sibling = buffer_lines_new_leaf_node(0);
        int sibling_count = node->line_count - split_at;
        memcpy(sibling->lines, &node->lines[split_at], sibling_count * sizeof(BufferLine*));
        sibling->line_count = sibling_count;
        node->line_count = split_at;
        return sibling;
    }

    // Case 2: We are at an INTERNAL node.
    node->line_count++; // The total count in this subtree increases.

    // Find which child to descend into.
    int child_index = 0;
    for (child_index = 0; child_index < node->children_count; ++child_index) {
        BufferLinesNode *child = node->children[child_index];
        if (position <= child->line_count) {
            break;
        }
        position -= child->line_count;
    }
    if (child_index == node->children_count) {
        child_index--;
        position = node->children[child_index]->line_count;
    }

    // Recursively insert.
    BufferLinesNode *new_sibling = insert_recursive(node->children[child_index], line, position);

    // If the child didn't split, we're done.
    if (!new_sibling) {
        return NULL;
    }

    // The child split, so we must add its new sibling to this node.
    memmove(&node->children[child_index + 2], &node->children[child_index + 1], (node->children_count - (child_index + 1)) * sizeof(BufferLinesNode*));
    node->children[child_index + 1] = new_sibling;
    node->children_count++;

    // If this node is not full, we're done.
    if (node->children_count < MAX_CHILDREN) {
        return NULL;
    }

    // Otherwise, SPLIT this internal node.
    int split_at = MAX_CHILDREN / 2;
    BufferLinesNode *sibling = buffer_lines_new_internal_node(0);
    int sibling_count = node->children_count - split_at;
    memcpy(sibling->children, &node->children[split_at], sibling_count * sizeof(BufferLinesNode*));
    sibling->children_count = sibling_count;
    node->children_count = split_at;
    
    // Recalculate the line counts for the two split nodes.
    node->line_count = 0;
    for (int i = 0; i < node->children_count; ++i) node->line_count += node->children[i]->line_count;
    sibling->line_count = 0;
    for (int i = 0; i < sibling->children_count; ++i) sibling->line_count += sibling->children[i]->line_count;
    
    return sibling;
}

/**
 * Deletes a line from the tree at a given position.
 * Handles root collapsing and tree destruction.
 * @param root_ptr A pointer to the root node pointer.
 * @param position The zero-based line number to delete.
 */
void buffer_lines_delete_line(BufferLinesNode **root_ptr, int position) {
    if (!root_ptr || !*root_ptr || position >= (*root_ptr)->line_count) {
        return; // Nothing to delete.
    }

    delete_recursive(*root_ptr, position);

    BufferLinesNode *root = *root_ptr;

    // If the root is an internal node with only one child, collapse the tree.
    if (root->children != NULL && root->children_count == 1) {
        *root_ptr = root->children[0];
        free(root->children);
        free(root);
    } 
    // If the last line was deleted from the root leaf.
    else if (root->children == NULL && root->line_count == 0) {
        free(root->lines);
        free(root);
        *root_ptr = NULL;
    }
}

/**
 * Recursive helper for deletion.
 */
static void delete_recursive(BufferLinesNode *node, int position) {
    node->line_count--;

    // Case 1: We are at a LEAF node.
    if (node->children == NULL) {
        // Note: Assumes the caller manages BufferLine memory. We only remove the pointer.
        memmove(&node->lines[position], &node->lines[position + 1], (node->line_count - position) * sizeof(BufferLine*));
        return;
    }

    // Case 2: We are at an INTERNAL node.
    int child_index = 0;
    for (child_index = 0; child_index < node->children_count; ++child_index) {
        if (position < node->children[child_index]->line_count) break;
        position -= node->children[child_index]->line_count;
    }

    delete_recursive(node->children[child_index], position);
    
    // After deletion, check if the child is now underfull and needs rebalancing.
    if ((node->children[child_index]->children == NULL && node->children[child_index]->line_count < MIN_CHILDREN) ||
        (node->children[child_index]->children != NULL && node->children[child_index]->children_count < MIN_CHILDREN)) {
        rebalance_after_delete(node, child_index);
    }
}

/**
 * Rebalances the tree after a deletion causes a node to become underfull.
 * It will first try to borrow from a sibling, and if not possible, will merge.
 */
static void rebalance_after_delete(BufferLinesNode *parent, int child_index) {
    BufferLinesNode *child = parent->children[child_index];
    int min_items = (child->children == NULL) ? MIN_CHILDREN : MIN_CHILDREN;

    // Try to borrow from RIGHT sibling
    if (child_index + 1 < parent->children_count) {
        BufferLinesNode *right_sibling = parent->children[child_index + 1];
        int right_count = (right_sibling->children == NULL) ? right_sibling->line_count : right_sibling->children_count;
        if (right_count > min_items) {
            if (child->children == NULL) { // Leaf: borrow a line
                child->lines[child->line_count++] = right_sibling->lines[0];
                right_sibling->line_count--;
                memmove(&right_sibling->lines[0], &right_sibling->lines[1], right_sibling->line_count * sizeof(BufferLine*));
            } else { // Internal: borrow a child node
                BufferLinesNode* node_to_move = right_sibling->children[0];
                child->children[child->children_count++] = node_to_move;
                child->line_count += node_to_move->line_count;
                right_sibling->line_count -= node_to_move->line_count;
                right_sibling->children_count--;
                memmove(&right_sibling->children[0], &right_sibling->children[1], right_sibling->children_count * sizeof(BufferLinesNode*));
            }
            return;
        }
    }

    // Try to borrow from LEFT sibling
    if (child_index > 0) {
        BufferLinesNode *left_sibling = parent->children[child_index - 1];
        int left_count = (left_sibling->children == NULL) ? left_sibling->line_count : left_sibling->children_count;
        if (left_count > min_items) {
            if (child->children == NULL) { // Leaf: borrow a line
                memmove(&child->lines[1], &child->lines[0], child->line_count * sizeof(BufferLine*));
                child->lines[0] = left_sibling->lines[--left_sibling->line_count];
                child->line_count++;
            } else { // Internal: borrow a child node
                BufferLinesNode* node_to_move = left_sibling->children[--left_sibling->children_count];
                memmove(&child->children[1], &child->children[0], child->children_count * sizeof(BufferLinesNode*));
                child->children[0] = node_to_move;
                child->children_count++;
                child->line_count += node_to_move->line_count;
                left_sibling->line_count -= node_to_move->line_count;
            }
            return;
        }
    }

    // Cannot borrow, must MERGE
    if (child_index + 1 < parent->children_count) {
        // Merge with right sibling
        BufferLinesNode *right_sibling = parent->children[child_index + 1];
        if (child->children == NULL) { // Leaf merge
            memcpy(&child->lines[child->line_count], right_sibling->lines, right_sibling->line_count * sizeof(BufferLine*));
        } else { // Internal merge
            memcpy(&child->children[child->children_count], right_sibling->children, right_sibling->children_count * sizeof(BufferLinesNode*));
            child->children_count += right_sibling->children_count;
        }
        child->line_count += right_sibling->line_count;
        
        // Remove right sibling from parent
        free(right_sibling->lines ? (void *)right_sibling->lines : (void *)right_sibling->children);
        free(right_sibling);
        memmove(&parent->children[child_index + 1], &parent->children[child_index + 2], (parent->children_count - (child_index + 2)) * sizeof(BufferLinesNode*));
        parent->children_count--;
    } else {
        // Merge with left sibling (merge `child` into `left_sibling`)
        BufferLinesNode *left_sibling = parent->children[child_index - 1];
        if (left_sibling->children == NULL) { // Leaf merge
            memcpy(&left_sibling->lines[left_sibling->line_count], child->lines, child->line_count * sizeof(BufferLine*));
        } else { // Internal merge
            memcpy(&left_sibling->children[left_sibling->children_count], child->children, child->children_count * sizeof(BufferLinesNode*));
            left_sibling->children_count += child->children_count;
        }
        left_sibling->line_count += child->line_count;
        
        // Remove child from parent
        free(child->lines ? (void *)child->lines : (void *)child->children);
        free(child);
        memmove(&parent->children[child_index], &parent->children[child_index + 1], (parent->children_count - (child_index + 1)) * sizeof(BufferLinesNode*));
        parent->children_count--;
    }
}

/**
 * Recursive helper for buffer_lines_get_range.
 */
static int get_range_recursive(BufferLinesNode *node, int start_pos, int count, BufferLine **out_lines, int lines_collected) {
    if (count <= 0) {
        return lines_collected;
    }

    // Base Case: We are at a leaf node
    if (node->children == NULL) {
        int to_copy = (start_pos + count > node->line_count) ? node->line_count - start_pos : count;
        memcpy(out_lines + lines_collected, node->lines + start_pos, to_copy * sizeof(BufferLine*));
        return lines_collected + to_copy;
    }

    // Recursive Step: We are at an internal node
    for (int i = 0; i < node->children_count; ++i) {
        BufferLinesNode *child = node->children[i];
        if (start_pos < child->line_count) {
            // The range starts in or before this child.
            int new_lines_collected = get_range_recursive(child, start_pos, count, out_lines, lines_collected);
            int collected_in_call = new_lines_collected - lines_collected;

            count -= collected_in_call;
            lines_collected = new_lines_collected;
            start_pos = 0; // Subsequent children will be read from their start

            if (count <= 0) break; // We have collected all we need
        } else {
            // The range starts after this child, so skip it.
            start_pos -= child->line_count;
        }
    }
    return lines_collected;
}

/**
 * Fills a pre-allocated array with pointers to BufferLines from a specified range.
 * This is much more efficient than calling buffer_lines_get in a loop.
 * Returns the number of lines actually retrieved, which may be less than `count`
 * if the range extends past the end of the buffer.
 */
int buffer_lines_get_range(BufferLinesNode *root, int start_pos, int count, BufferLine **out_lines) {
    if (root == NULL || out_lines == NULL || count <= 0 || start_pos >= root->line_count) {
        return 0;
    }
    // Clamp count to what's available
    if (start_pos + count > root->line_count) {
        count = root->line_count - start_pos;
    }
    return get_range_recursive(root, start_pos, count, out_lines, 0);
}

/**
 * Converts a (line, column) coordinate to a single character offset from the
 * start of the buffer. Each newline character is counted as one character.
 * Note: This can be slow for large files as it iterates from the beginning.
 */
int buffer_lines_to_offset(BufferLinesNode *root, int line_pos, int col_pos) {
    if (root == NULL || line_pos >= root->line_count) return -1; // Invalid input

    int offset = 0;
    // Get all lines up to the target line to sum their lengths
    BufferLine **lines = malloc(line_pos * sizeof(BufferLine*));
    if (lines == NULL) return -1; // Out of memory

    int num_retrieved = buffer_lines_get_range(root, 0, line_pos, lines);

    for (int i = 0; i < num_retrieved; i++) {
        offset += lines[i]->char_count + 1; // +1 for the newline
    }
    free(lines);

    // Get the final line to handle the column position
    BufferLine *target_line = buffer_lines_get(root, line_pos);
    if (target_line == NULL) return -1;
    
    int target_len = target_line->char_count;
    if (col_pos > target_len) col_pos = target_len;
    
    offset += col_pos;
    
    return offset;
}

/**
 * Converts a character offset into a (line, column) coordinate.
 * Note: Like its inverse, this can be slow for large files.
 */
void buffer_lines_from_offset(BufferLinesNode *root, int offset, int *out_line_pos, int *out_col_pos) {
    if (root == NULL || offset < 0) {
        *out_line_pos = 0;
        *out_col_pos = 0;
        return;
    }

    int total_lines = buffer_lines_count(root);
    for (int i = 0; i < total_lines; i++) {
        BufferLine *line = buffer_lines_get(root, i); // Inefficient, but simple
        if (line == NULL) break; // Should not happen

        int line_len_with_newline = line->char_count + 1;
        if (offset < line_len_with_newline) {
            *out_line_pos = i;
            *out_col_pos = (int)offset;
            return;
        }
        offset -= line_len_with_newline;
    }

    // If offset is beyond the end of the file, place cursor at the end
    *out_line_pos = total_lines > 0 ? total_lines - 1 : 0;
    BufferLine* last_line = buffer_lines_get(root, *out_line_pos);
    *out_col_pos = last_line ? last_line->char_count : 0;
}
