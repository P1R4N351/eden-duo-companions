/* totk_byml.c: little-endian BYML v7 into a value tree (MIT, LICENSE.MIT). */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { TOTK_BYML_NULL, TOTK_BYML_STR, TOTK_BYML_ARR, TOTK_BYML_DICT, TOTK_BYML_BOOL, TOTK_BYML_INT, TOTK_BYML_UINT,
       TOTK_BYML_FLOAT };
/* One value. s points into the document, so the document must outlive the tree. Arrays and dictionaries hold n items
   (dictionaries also n keys, in the document's order). */
typedef struct TotkBymlVal {
    int type;
    int64_t i;
    uint64_t u;
    double f;
    const char *s;
    size_t n;
    struct TotkBymlVal *items;
    const char **keys;
} TotkBymlVal;
/* The document d[0..n) ("YB", version 7, the subset TotK's RSDB tables use): a tree to free with totk_byml_free, or NULL
   when the document is not one or is damaged. */
TotkBymlVal *totk_byml(const uint8_t *d, size_t n);
void totk_byml_free(TotkBymlVal *v);
/* A dictionary's value for key (the last one wins); NULL when absent or v is not a dictionary. */
const TotkBymlVal *totk_byml_get(const TotkBymlVal *dict, const char *key);
/* A number as double (bool, int, uint, float); fallback otherwise. */
double totk_byml_num(const TotkBymlVal *v, double fallback);
#ifdef __cplusplus
}
#endif
