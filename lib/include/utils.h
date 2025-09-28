#ifndef UTILS_H
#define UTILS_H

#include <stddef.h> //size_t
#include <stdint.h>

void hexdump(const void *data, size_t len);
int is_alnum_n(const uint8_t *s, size_t n);

#endif
