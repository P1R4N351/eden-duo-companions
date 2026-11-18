/* Little-endian BYML (v7) into a value tree, for the RSDB tables the Tears of the Kingdom page reads from the player's
   own romfs (RSDB/Challenge.Product.<v>.rstbl.byml.zs: the quest catalogue, totk_quests.c). MIT, like the reader
   (LICENSE.MIT).

   Not new code: this is TotK Native's BYML reader, the cg_byml() of its asset stager
   (P1R4N351/botw-android games/totk/android/setup/companion/cg_formats.c, commit 29b4924c, MIT), with its types renamed
   (CgVal -> TotkBymlVal, cg_* -> totk_byml_*) and its error reporting reduced to a NULL result. The parsing itself is
   unchanged: string tables, 0xa0 string, 0xc0 array, 0xc1 dictionary, 0xd0 bool, 0xd1 int, 0xd2 float, 0xd3 uint,
   0xd4-0xd6 64-bit values, 0xff null; every offset and count is bounded by the document, nesting by 64. */
#include "totk_byml.h"
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t rd64(const uint8_t *p) { return rd32(p) | (uint64_t)rd32(p + 4) << 32; }

typedef struct { const uint8_t *d; size_t n; const char **keys, **values; size_t nkeys, nvalues; } Byml;
static int byml_strings(const uint8_t *d, size_t n, uint32_t at, const char ***out, size_t *count) {
    *out = NULL; *count = 0;
    if (!at) return 0;
    if (at > n - 4) return -1;
    uint32_t cnt = rd32(d + at) >> 8;
    if (cnt > (n - at - 4) / 4) return -1;
    const char **s = calloc(cnt ? cnt : 1, sizeof *s);
    if (!s) return -1;
    for (uint32_t i = 0; i < cnt; i++) {
        uint64_t o = (uint64_t)at + rd32(d + at + 4 + i * 4);
        if (o >= n || !memchr(d + o, 0, n - o)) { free(s); return -1; }
        s[i] = (const char *)d + o;
    }
    *out = s; *count = cnt;
    return 0;
}
static int byml_value(const Byml *b, unsigned t, uint32_t v, TotkBymlVal *out, int depth) {
    memset(out, 0, sizeof *out);
    const uint8_t *d = b->d; size_t n = b->n;
    if (depth > 64) return -1;
    switch (t) {
    case 0xa0: if (v >= b->nvalues) return -1; out->type = TOTK_BYML_STR; out->s = b->values[v]; return 0;
    case 0xc0: case 0xc1: {
        if (v > n - 4) return -1;
        uint32_t cnt = rd32(d + v) >> 8;
        out->type = t == 0xc0 ? TOTK_BYML_ARR : TOTK_BYML_DICT;
        if (!(out->items = calloc(cnt ? cnt : 1, sizeof *out->items))) return -1;
        out->n = cnt;
        if (t == 0xc0) {
            uint64_t start = ((uint64_t)v + 4 + cnt + 3) & ~3ull;
            if (start > n || cnt > (n - start) / 4) return -1;
            for (uint32_t i = 0; i < cnt; i++)
                if (byml_value(b, d[v + 4 + i], rd32(d + start + i * 4), &out->items[i], depth + 1)) return -1;
            return 0;
        }
        if (cnt > (n - v - 4) / 8 || !(out->keys = calloc(cnt ? cnt : 1, sizeof *out->keys))) return -1;
        for (uint32_t i = 0; i < cnt; i++) {
            const uint8_t *a = d + v + 4 + i * 8;
            uint32_t k = rd32(a) & 0xffffff;
            if (k >= b->nkeys) return -1;
            out->keys[i] = b->keys[k];
            if (byml_value(b, a[3], rd32(a + 4), &out->items[i], depth + 1)) return -1;
        }
        return 0;
    }
    case 0xd0: out->type = TOTK_BYML_BOOL; out->i = v != 0; return 0;
    case 0xd1: out->type = TOTK_BYML_INT; out->i = (int32_t)v; return 0;
    case 0xd2: { float f; memcpy(&f, &v, 4); out->type = TOTK_BYML_FLOAT; out->f = f; return 0; }
    case 0xd3: out->type = TOTK_BYML_UINT; out->u = v; return 0;
    case 0xd4: case 0xd5: case 0xd6:
        if (v > n - 8) return -1;
        if (t == 0xd4) { out->type = TOTK_BYML_INT; out->i = (int64_t)rd64(d + v); }
        else if (t == 0xd5) { out->type = TOTK_BYML_UINT; out->u = rd64(d + v); }
        else { uint64_t u = rd64(d + v); out->type = TOTK_BYML_FLOAT; memcpy(&out->f, &u, 8); }
        return 0;
    case 0xff: out->type = TOTK_BYML_NULL; return 0;
    default: return -1;
    }
}
static void val_clear(TotkBymlVal *v) {
    if (v->type == TOTK_BYML_ARR || v->type == TOTK_BYML_DICT)
        for (size_t i = 0; i < v->n && v->items; i++) val_clear(&v->items[i]);
    free(v->items); free(v->keys);
}
void totk_byml_free(TotkBymlVal *v) { if (v) { val_clear(v); free(v); } }
TotkBymlVal *totk_byml(const uint8_t *d, size_t n) {
    Byml b = { d, n, NULL, NULL, 0, 0 };
    TotkBymlVal *root = calloc(1, sizeof *root);
    if (!root) return NULL;
    if (n < 16 || d[0] != 'Y' || d[1] != 'B') goto fail;
    uint32_t kt = rd32(d + 4), st = rd32(d + 8), at = rd32(d + 12);
    if (byml_strings(d, n, kt, &b.keys, &b.nkeys) || byml_strings(d, n, st, &b.values, &b.nvalues) || at >= n ||
        byml_value(&b, d[at], at, root, 0)) goto fail;
    free(b.keys); free(b.values);
    return root;
fail:
    free(b.keys); free(b.values); totk_byml_free(root);
    return NULL;
}
const TotkBymlVal *totk_byml_get(const TotkBymlVal *dict, const char *key) {
    if (!dict || dict->type != TOTK_BYML_DICT) return NULL;
    for (size_t i = dict->n; i-- > 0;) if (!strcmp(dict->keys[i], key)) return &dict->items[i];   /* the last wins, as dict() */
    return NULL;
}
double totk_byml_num(const TotkBymlVal *v, double fallback) {
    if (!v) return fallback;
    switch (v->type) {
    case TOTK_BYML_BOOL: case TOTK_BYML_INT: return (double)v->i;
    case TOTK_BYML_UINT: return (double)v->u;
    case TOTK_BYML_FLOAT: return v->f;
    default: return fallback;
    }
}
