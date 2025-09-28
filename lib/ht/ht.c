#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#include <stddef.h>
#endif

#include "ht.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static inline uint64_t hash64_fnv1a(const char *s) {
  uint64_t hash = 0xcbf29ce484222325;
  for (size_t i = 0; s[i] != '\0'; ++i) {
    hash ^= (uint64_t)s[i];
    hash *= (uint64_t)s[i];
  }
  return hash;
}

typedef struct bucket {
  char *key;
  void *value;
  struct bucket *next;
} bucket;

struct ht {
  bucket **buckets;
  uint64_t n; // number of entries occupied in the hash table
  uint64_t m; // size of table
};

ht *ht_new(size_t m) {
  ht *t = calloc(1, sizeof *t);
  if (!t)
    return NULL;
  t->buckets = calloc(m, sizeof *t->buckets); // array of bucket*
  if (!t->buckets) {
    free(t);
    return NULL;
  }
  t->m = m;
  return t;
}

void ht_free(ht *t) {
  if (!t)
    return;
  if (t->buckets) {
    for (size_t i = 0; i < t->m; i++) {
      for (bucket *p = t->buckets[i]; p;) {
        bucket *n = p->next;
        free(p->key); // own keys
        /* do NOT free(p->value); */
        free(p);
        p = n;
      }
    }
    free(t->buckets);
  }
  free(t);
}

static void ht_rehash(ht *t, size_t new_m) {
  bucket **nb = calloc(new_m, sizeof *nb);
  if (!nb)
    return; // skip grow on OOM

  for (size_t i = 0; i < t->m; i++) {
    for (bucket *p = t->buckets[i]; p;) {
      bucket *nxt = p->next;
      size_t j = hash64_fnv1a(p->key) % new_m;
      p->next = nb[j];
      nb[j] = p;
      p = nxt;
    }
  }
  free(t->buckets);
  t->buckets = nb;
  t->m = new_m;
}

int ht_set(ht *t, const char *k, void *v) {
  if (!t || t->m == 0 || !t->buckets)
    return -1;

  size_t idx = hash64_fnv1a(k) % t->m;

  for (bucket *p = t->buckets[idx]; p; p = p->next) {
    if (strcmp(p->key, k) == 0) {
      p->value = v;
      return 0;
    }
  }

  bucket *buck = malloc(sizeof *buck);
  if (!buck)
    return -1;
  buck->key = strdup(k);
  if (!buck->key) {
    free(buck);
    return -1;
  }
  buck->value = v;
  buck->next = t->buckets[idx];
  t->buckets[idx] = buck;
  t->n++;

  if (t->n * 4 > t->m * 3)
    ht_rehash(t, t->m ? t->m * 2 : 1);
  return 0;
}

void *ht_get(const ht *t, const char *k) {
  uint64_t idx = hash64_fnv1a(k) % t->m;

  bucket *head = t->buckets[idx];
  while (head != NULL) {
    if (strcmp(head->key, k) == 0) {
      return head->value;
    }
    head = head->next;
  }

  return NULL;
}

int ht_del(ht *t, const char *k) {
  uint64_t idx = hash64_fnv1a(k) % t->m;

  bucket **pp = &t->buckets[idx];
  for (bucket *p = *pp; p; p = p->next) {
    // p walks the nodes: head, head->next, ...
    // pp stays one step “behind”, pointing to the link that leads to p.
    if (strcmp(p->key, k) == 0) {
      *pp = p->next;
      free(p->key);
      free(p);
      return 0;
    }
    pp = &p->next;
  }

  return -1; // not found
}

size_t ht_len(const ht *m) { return m->n; }
