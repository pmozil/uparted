#ifndef XALLOC_H_
#define XALLOC_H_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef size_t idx_t;

void xalloc_die();
void *xmalloc(size_t s);
void *ximalloc(idx_t s);
void *xinmalloc(idx_t n, idx_t s);
void *xzalloc(size_t s);
void *xizalloc(idx_t s);
void *xcalloc(size_t n, size_t s);
void *xicalloc(idx_t n, idx_t s);
void *xrealloc(void *p, size_t s);
void *xirealloc(void *p, idx_t s);
void *xreallocarray(void *p, size_t n, size_t s);
void *xireallocarray(void *p, idx_t n, idx_t s);
void *x2realloc(void *p, size_t *ps);
void *x2nrealloc(void *p, size_t *pn, size_t s);
void *xpalloc(void *pa, idx_t *pn, idx_t n_incr_min, ptrdiff_t n_max, idx_t s);
void *xmemdup(void const *p, size_t s);
void *ximemdup(void const *p, idx_t s);
char *ximemdup0(void const *p, idx_t s);
char *xstrdup(char const *str);

#endif
