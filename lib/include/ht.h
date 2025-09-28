#pragma once
#include <stddef.h>

typedef struct ht ht;

ht *ht_new(size_t initial_cap);
void ht_free(ht *m);
int ht_set(ht *m, const char *key, void *val);
void *ht_get(const ht *m, const char *key);
int ht_del(ht *m, const char *key);
size_t ht_len(const ht *m);
