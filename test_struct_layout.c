#include <stdio.h>
#include <stddef.h>

typedef struct {
    char value[5];
    int width;
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char italic;
    unsigned char bold;
    unsigned char underline;
} CharIntWidth;

typedef struct {
    char value[5];
    unsigned char width;
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char italic;
    unsigned char bold;
    unsigned char underline;
} CharUnsignedCharWidth;

int main() {
    printf("CharIntWidth:\n");
    printf("  sizeof: %zu\n", sizeof(CharIntWidth));
    printf("  offsetof(value): %zu\n", offsetof(CharIntWidth, value));
    printf("  offsetof(width): %zu\n", offsetof(CharIntWidth, width));
    printf("  offsetof(r): %zu\n", offsetof(CharIntWidth, r));
    printf("  offsetof(g): %zu\n", offsetof(CharIntWidth, g));
    printf("  offsetof(b): %zu\n", offsetof(CharIntWidth, b));
    printf("  offsetof(italic): %zu\n", offsetof(CharIntWidth, italic));
    printf("  offsetof(bold): %zu\n", offsetof(CharIntWidth, bold));
    printf("  offsetof(underline): %zu\n", offsetof(CharIntWidth, underline));

    printf("\nCharUnsignedCharWidth:\n");
    printf("  sizeof: %zu\n", sizeof(CharUnsignedCharWidth));
    printf("  offsetof(value): %zu\n", offsetof(CharUnsignedCharWidth, value));
    printf("  offsetof(width): %zu\n", offsetof(CharUnsignedCharWidth, width));
    printf("  offsetof(r): %zu\n", offsetof(CharUnsignedCharWidth, r));
    printf("  offsetof(g): %zu\n", offsetof(CharUnsignedCharWidth, g));
    printf("  offsetof(b): %zu\n", offsetof(CharUnsignedCharWidth, b));
    printf("  offsetof(italic): %zu\n", offsetof(CharUnsignedCharWidth, italic));
    printf("  offsetof(bold): %zu\n", offsetof(CharUnsignedCharWidth, bold));
    printf("  offsetof(underline): %zu\n", offsetof(CharUnsignedCharWidth, underline));

    return 0;
}
