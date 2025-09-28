#define _POSIX_C_SOURCE 200809L

#include "acutest.h"
#include "ht.h" // provides: ht, ht_new, ht_set, ht_get, ht_free
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifndef TEST_REQUIRE_
#define TEST_REQUIRE_(cond, ...)                                               \
  do {                                                                         \
    TEST_CHECK_(cond, __VA_ARGS__);                                            \
    if (!(cond))                                                               \
      return;                                                                  \
  } while (0)
#endif

// Optional trace: enable with V=1
#define TRACE(...)                                                             \
  do {                                                                         \
    if (getenv("V"))                                                           \
      fprintf(stderr, __VA_ARGS__);                                            \
  } while (0)

static void t_insert_get_one(void) {
  const char *key = "myKey";
  int value = 16;

  ht *m = ht_new(8);
  TEST_REQUIRE_(m != NULL, "ht_new failed");

  int rc = ht_set(m, key, &value);
  TEST_REQUIRE_(rc == 0, "ht_set rc=%d key=%s", rc, key);

  void *p = ht_get(m, key);
  TEST_CHECK_(p == &value, "ht_get returned %p expected %p", p, (void *)&value);

  TRACE("insert_get ok: key=%s value=%d ptr=%p\n", key, value, (void *)&value);
  ht_free(m);
}

static void t_insert_get_many(void) {
  struct {
    const char *k;
    int v;
  } items[] = {
      {"k0", 0},     {"k1", 1},      {"alpha", 42}, {"beta", 7},
      {"gamma", -3}, {"delta", 999}, {"z", 5},      {"long_key_name", 1234},
  };
  const size_t N = sizeof(items) / sizeof(items[0]);

  ht *m = ht_new(8);
  TEST_REQUIRE_(m != NULL, "ht_new failed");

  // Insert all
  for (size_t i = 0; i < N; i++) {
    int rc = ht_set(m, items[i].k, &items[i].v);
    TEST_CHECK_(rc == 0, "ht_set rc=%d key=%s idx=%zu", rc, items[i].k, i);
    if (rc != 0)
      return; // bail early to avoid cascades
  }

  // Verify all
  for (size_t i = 0; i < N; i++) {
    void *p = ht_get(m, items[i].k);
    TEST_CHECK_(p == &items[i].v, "ht_get key=%s idx=%zu got=%p want=%p",
                items[i].k, i, p, (void *)&items[i].v);
  }

  // Optional: check a miss returns NULL if that’s your API
  TEST_CHECK_(ht_get(m, "nope") == NULL, "expected miss to be NULL");

  ht_free(m);
}

// Overwrite : same key, new value replaces old
static void t_overwrite(void) {
  const char *key = "same";
  int v1 = 111, v2 = 222;

  ht *m = ht_new(8);
  TEST_REQUIRE_(m != NULL, "ht_new failed");

  TEST_REQUIRE_(ht_set(m, key, &v1) == 0, "set v1 failed");
  void *p1 = ht_get(m, key);
  TEST_CHECK_(p1 == &v1, "after first set got=%p want=%p", p1, (void *)&v1);

  TEST_REQUIRE_(ht_set(m, key, &v2) == 0, "overwrite v2 failed");
  void *p2 = ht_get(m, key);
  TEST_CHECK_(p2 == &v2, "after overwrite got=%p want=%p", p2, (void *)&v2);

  ht_free(m);
}

// Delete: remove key, then ensure miss; deleting again returns not-found
static void t_delete(void) {
  const char *k1 = "a", *k2 = "b";
  int v1 = 1, v2 = 2;

  ht *m = ht_new(8);
  TEST_REQUIRE_(m != NULL, "ht_new failed");

  TEST_REQUIRE_(ht_set(m, k1, &v1) == 0, "set k1 failed");
  TEST_REQUIRE_(ht_set(m, k2, &v2) == 0, "set k2 failed");

  TEST_REQUIRE_(ht_get(m, k1) == &v1, "pre-del get k1 mismatch");

  int rc = ht_del(m, k1);
  TEST_CHECK_(rc == 0, "del k1 rc=%d", rc);

  TEST_CHECK_(ht_get(m, k1) == NULL, "k1 should be gone");
  TEST_CHECK_(ht_get(m, k2) == &v2, "k2 should remain");

  int rc2 = ht_del(m, k1);
  TEST_CHECK_(rc2 != 0, "second delete should report not-found");

  ht_free(m);
}

static void t_collisions_chain(void) {
  ht *m = ht_new(1); // force all keys into bucket 0
  TEST_REQUIRE_(m, "ht_new");

  int v0 = 0, v1 = 1, v2 = 2, v3 = 3;
  TEST_REQUIRE_(ht_set(m, "a", &v0) == 0, "set a");
  TEST_REQUIRE_(ht_set(m, "b", &v1) == 0, "set b");
  TEST_REQUIRE_(ht_set(m, "c", &v2) == 0, "set c");
  TEST_REQUIRE_(ht_set(m, "d", &v3) == 0, "set d");

  TEST_CHECK_(ht_get(m, "a") == &v0, "get a");
  TEST_CHECK_(ht_get(m, "b") == &v1, "get b");
  TEST_CHECK_(ht_get(m, "c") == &v2, "get c");
  TEST_CHECK_(ht_get(m, "d") == &v3, "get d");

  ht_free(m);
}

static void t_key_is_copied(void) {
  ht *m = ht_new(8);
  TEST_REQUIRE_(m, "ht_new");

  char keybuf[8];
  strcpy(keybuf, "alpha");
  int v = 123;
  TEST_REQUIRE_(ht_set(m, keybuf, &v) == 0, "set alpha");

  // Mutate caller buffer; table should still find by original spelling
  keybuf[0] = 'X';
  TEST_CHECK_(ht_get(m, "alpha") == &v, "lookup by original key");

  ht_free(m);
}

static void t_delete_positions(void) {
  ht *m = ht_new(1); // single bucket, list order will be reverse of inserts if
                     // you insert-at-head
  TEST_REQUIRE_(m, "ht_new");

  int v1 = 1, v2 = 2, v3 = 3;
  TEST_REQUIRE_(ht_set(m, "k1", &v1) == 0, "set k1");
  TEST_REQUIRE_(ht_set(m, "k2", &v2) == 0, "set k2");
  TEST_REQUIRE_(ht_set(m, "k3", &v3) == 0, "set k3");

  // Delete head of list (likely "k3")
  TEST_CHECK_(ht_del(m, "k3") == 0, "del k3");
  TEST_CHECK_(ht_get(m, "k3") == NULL, "k3 gone");
  TEST_CHECK_(ht_get(m, "k2") == &v2, "k2 stays");
  TEST_CHECK_(ht_get(m, "k1") == &v1, "k1 stays");

  // Delete middle ("k2")
  TEST_CHECK_(ht_del(m, "k2") == 0, "del k2");
  TEST_CHECK_(ht_get(m, "k2") == NULL, "k2 gone");
  TEST_CHECK_(ht_get(m, "k1") == &v1, "k1 stays");

  // Delete tail ("k1")
  TEST_CHECK_(ht_del(m, "k1") == 0, "del k1");
  TEST_CHECK_(ht_get(m, "k1") == NULL, "k1 gone");

  // Deleting missing key should report not-found
  TEST_CHECK_(ht_del(m, "nope") != 0, "del missing");

  ht_free(m);
}

static uint64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void t_rehash_stress(void) {
  const size_t N = 200000; // bump to taste
  ht *m = ht_new(1);       // pathological: all collide
  TEST_REQUIRE_(m, "ht_new");

  // Values live in an array so ht stores stable pointers
  int *vals = malloc(N * sizeof *vals);
  TEST_REQUIRE_(vals, "malloc vals");

  uint64_t t0 = now_ms();

  // Insert N distinct keys
  for (size_t i = 0; i < N; i++) {
    vals[i] = (int)i;
    char key[32];
    int n = snprintf(key, sizeof key, "k%zu", i);
    TEST_REQUIRE_(n > 0 && (size_t)n < sizeof key, "snprintf key");
    TEST_REQUIRE_(ht_set(m, key, &vals[i]) == 0, "ht_set #%zu", i);
  }

  // Probe a few lookups
  for (size_t i = 0; i < 1000; i += 137) {
    char key[32];
    snprintf(key, sizeof key, "k%zu", i);
    void *p = ht_get(m, key);
    TEST_CHECK_(p == &vals[i], "get %s: got=%p want=%p", key, p,
                (void *)&vals[i]);
  }

  uint64_t elapsed = now_ms() - t0;

  // Threshold: tuned for “rehash exists”. Override via STRESS_MS env.
  long limit = 3000; // ms
  const char *env = getenv("STRESS_MS");
  if (env && *env) {
    char *end;
    long v = strtol(env, &end, 10);
    if (end != env && v > 0)
      limit = v;
  }

  TEST_CHECK_(elapsed <= (uint64_t)limit,
              "stress took %llums (> %ldms). Likely no rehash.",
              (unsigned long long)elapsed, limit);

  free(vals);
  ht_free(m);
}

TEST_LIST = {{"insert_get_one", t_insert_get_one},
             {"insert_get_many", t_insert_get_many},
             {"overwrite", t_overwrite},
             {"delete", t_delete},
             {"collisions_chain", t_collisions_chain},
             {"key_is_copied", t_key_is_copied},
             {"delete_positions", t_delete_positions},
             {"rehash_stress", t_rehash_stress},
             {NULL, NULL}};
