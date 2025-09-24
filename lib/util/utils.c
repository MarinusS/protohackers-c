#include "utils.h"
#include <ctype.h>
#include <stdio.h>

void hexdump(const void *data, size_t len) {
    const unsigned char *p = data;
    for (size_t off = 0; off < len; off += 16) {
        size_t n = (len - off < 16) ? (len - off) : 16;

        // offset
        printf("%08zx  ", off);

        // hex column
        for (size_t i = 0; i < 16; i++) {
            if (i < n) printf("%02X ", p[off + i]);
            else       fputs("   ", stdout);
            if (i == 7) putchar(' '); // extra space in middle
        }

        // ASCII column
        fputs(" |", stdout);
        for (size_t i = 0; i < n; i++) {
            unsigned char c = p[off + i];
            putchar(isprint(c) ? c : '.');
        }
        fputs("|\n", stdout);
    }
}
