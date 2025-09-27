#include "utils.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>

#define JSON_INT_SAFE_MAX 9007199254740991.0 /* 2^53-1 */

void hexdump(const void *data, size_t len) {
  const unsigned char *p = data;
  for (size_t off = 0; off < len; off += 16) {
    size_t n = (len - off < 16) ? (len - off) : 16;

    // offset
    printf("%08zx  ", off);

    // hex column
    for (size_t i = 0; i < 16; i++) {
      if (i < n)
        printf("%02X ", p[off + i]);
      else
        fputs("   ", stdout);
      if (i == 7)
        putchar(' '); // extra space in middle
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
