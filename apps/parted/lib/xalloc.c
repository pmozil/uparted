#include <string.h>
#include <xalloc.h>

void xalloc_die() {
    fprintf(stderr, "\nNo memory left!\n");
    _Exit(1);
}

void *xmalloc(size_t s) {
    void *val = malloc(s);
    if (val == NULL) {
        xalloc_die();
    }
}

void *ximalloc(idx_t s) { return xmalloc(s); }

void *xinmalloc(idx_t n, idx_t s) { return xmalloc(n * s); }

void *xzalloc(size_t s) {
    void *val = xmalloc(s);
    for (int i = 0; i < s; i++) {
        ((char *)val)[i] = 0;
    }
    return val;
}
void *xizalloc(idx_t s) { return xmalloc(s); }
void *xcalloc(size_t n, size_t s) { return xmalloc(n * s); }
void *xicalloc(idx_t n, idx_t s) { return xcalloc(n, s); }
void *xrealloc(void *p, size_t s) {
    void *val = realloc(p, s);
    if (val == NULL) {
        xalloc_die();
    }
    return val;
}
void *xirealloc(void *p, idx_t s) { return xrealloc(p, s); }
void *xreallocarray(void *p, size_t n, size_t s) { return xrealloc(p, n * s); }
void *xireallocarray(void *p, idx_t n, idx_t s) {
    return xreallocarray(p, n, s);
}
void *x2realloc(void *p, size_t *ps) { return x2nrealloc(p, ps, 1); }

void *xmemdup(void const *p, size_t s) {
    void *val = xmalloc(s);
    for (int i = 0; i < s; i++) {
        ((char *)val)[i] = ((char *)p)[i];
    }
    return val;
}
void *ximemdup(void const *p, idx_t s) { return xmemdup(p, s); }
char *ximemdup0(void const *p, idx_t s) { return xmemdup(p, s); }
char *xstrdup(char const *str) {
    size_t len = strlen(str);
    return xmemdup(str, len);
}

void *x2nrealloc(void *p, size_t *pn, size_t s) {
    size_t n = *pn;

    if (!p) {
        if (!n) {
            /* The approximate size to use for initial small allocation
               requests, when the invoking code specifies an old size of
               zero.  This is the largest "small" request for the GNU C
               library malloc.  */
            enum { DEFAULT_MXFAST = 64 * sizeof(size_t) / 4 };

            n = DEFAULT_MXFAST / s;
            n += !n;
        }
    } else {
        /* Set N = floor (1.5 * N) + 1 to make progress even if N == 0.  */
        size_t n1 = n + (n >> 1) + 1;
        if (n1 < n)
            xalloc_die();
        n = n1;
    }

    p = xreallocarray(p, n, s);
    *pn = n;
    return p;
}

int ckd_add(size_t *n, size_t v0, size_t v1) {
    size_t res = v0 + v1;
    if (res < v0 || res < v1) {
        return 1;
    }
    *n = res;
    return 0;
}

int ckd_mul(size_t *n, size_t v0, size_t v1) {
    size_t res = v0 * v1;
    if ((res < v0 && v0 != 0) || (v1 != 0 && res < v1)) {
        return 1;
    }
    *n = res;
    return 0;
}

void *xpalloc(void *pa, idx_t *pn, idx_t n_incr_min, ptrdiff_t n_max, idx_t s) {
    idx_t n0 = *pn;

    /* The approximate size to use for initial small allocation
       requests.  This is the largest "small" request for the GNU C
       library malloc.  */
    enum { DEFAULT_MXFAST = 64 * sizeof(size_t) / 4 };

    /* If the array is tiny, grow it to about (but no greater than)
       DEFAULT_MXFAST bytes.  Otherwise, grow it by about 50%.
       Adjust the growth according to three constraints: N_INCR_MIN,
       N_MAX, and what the C language can represent safely.  */

    idx_t n;
    if (ckd_add(&n, n0, n0 >> 1))
        n = INT_MAX;
    if (0 <= n_max && n_max < n)
        n = n_max;

    /* NBYTES is of a type suitable for holding the count of bytes in an
       object. This is typically idx_t, but it should be size_t on
       (theoretical?) platforms where SIZE_MAX < IDX_MAX so xpalloc does not
       pass values greater than SIZE_MAX to xrealloc.  */
    size_t nbytes;
    idx_t adjusted_nbytes = (ckd_mul(&nbytes, n, s)    ? SIZE_MAX
                             : nbytes < DEFAULT_MXFAST ? DEFAULT_MXFAST
                                                       : 0);
    if (adjusted_nbytes) {
        n = adjusted_nbytes / s;
        nbytes = adjusted_nbytes - adjusted_nbytes % s;
    }

    if (!pa)
        *pn = 0;
    if (n - n0 < n_incr_min &&
        (ckd_add(&n, n0, n_incr_min) || (0 <= n_max && n_max < n) ||
         ckd_mul(&nbytes, n, s)))
        xalloc_die();
    pa = xrealloc(pa, nbytes);
    *pn = n;
    return pa;
}
