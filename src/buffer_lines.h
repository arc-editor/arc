#ifndef BUFFER_LINES_H
#define BUFFER_LINES_H

#include "buffer.h"

typedef struct BufferLinesNode {
  BufferLine **lines;
  int line_count;
  int offset;
  struct BufferLinesNode **children;
  int children_count;
} BufferLinesNode;

BufferLinesNode *buffer_lines_new_leaf_node(int offset);
BufferLinesNode *buffer_lines_new_internal_node(int offset);
void buffer_lines_insert_line(BufferLinesNode **root, BufferLine *line, int position);
void buffer_lines_delete_line(BufferLinesNode **root, int position);
BufferLine *buffer_lines_get(BufferLinesNode *root, int position);
int buffer_lines_count(BufferLinesNode *root);
void buffer_lines_free_node(BufferLinesNode *root);
void buffer_lines_from_offset(BufferLinesNode *root, int offset, int *out_line_pos, int *out_col_pos);
int buffer_lines_to_offset(BufferLinesNode *root, int line_pos, int col_pos);
int buffer_lines_get_range(BufferLinesNode *root, int start_pos, int count, BufferLine **out_lines);

#endif
