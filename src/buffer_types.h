#ifndef BUFFER_TYPES_H
#define BUFFER_TYPES_H

typedef struct __attribute__((packed)) {
    char value[5];
    unsigned char width;
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char style;
} Char;

#define STYLE_ITALIC 1
#define STYLE_BOLD 2
#define STYLE_UNDERLINE 4

typedef struct {
    int char_count;
    int capacity;
    Char *chars;
    int needs_highlight;
} BufferLine;

typedef struct BufferLinesNode {
  BufferLine **lines;
  int line_count;
  int offset;
  struct BufferLinesNode **children;
  int children_count;
} BufferLinesNode;

#endif
