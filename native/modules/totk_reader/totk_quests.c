/* The quest catalogue and the quest the Ready page's left pager shows. MIT, like the reader (LICENSE.MIT).

   Everything comes from the player's own files at runtime; no quest name, hash or position is built in.
   - The catalogue is RSDB/Challenge.Product.<v>.rstbl.byml.zs of the romfs (1.4.3: v 143, 285 rows), parsed with
     totk_byml.c (TotK Native's BYML reader). Per row: __RowId "Work/Challenge/<Name>.game__challenge__Challenge.gyml",
     Category (Main / Sub / ImportantMini / none), SortIndex (missing on 31 rows: sorted last), IsListAdventureLog
     (default true; false on 32 minigame rows), Steps[] {Name, IsNotChangeAdventureLog, DestinationPoint[]}. A
     destination is a DestinationPos {Pos {X, Y, Z}} or a DestinationActor {..., AlternativePos {X, Y, Z}}, each with an
     optional BlackOutFlag (a Bool or BoolExp name); the walker takes Pos, else AlternativePos, wherever the entry nests
     it, so either way the table spells the type it reads the same coordinates.
   - The state is GameData (totk_gamedata.c totk_quest_state): Step_<Name> holds companion_hash of the current step's
     name, of "NotReady" or of "Complete"; SelectChallengeHash holds companion_hash(<Name>) of the tracked quest.
   - The display text (quest names and step objectives) is the module's: ChallengeMsg/Info_<Name>.msbt of the player's
     message archive, through its MSBT reader.
   Verified against the 1.4.3 romfs and the owner's saves by static analysis (android/APP.md, "Second-screen quests");
   not run on a device yet. */
#include "totk_quests.h"
#include "companion.h"
#include "totk_byml.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char ROW_PREFIX[] = "Work/Challenge/", ROW_SUFFIX[] = ".game__challenge__Challenge.gyml";

static int row_name(const char *row, char out[TOTK_QUEST_NAME]) {
    size_t lp = sizeof ROW_PREFIX - 1, ls = sizeof ROW_SUFFIX - 1, n = row ? strlen(row) : 0;
    if (n <= lp + ls || strncmp(row, ROW_PREFIX, lp) || strcmp(row + n - ls, ROW_SUFFIX)) return 0;
    size_t len = n - lp - ls;
    if (len >= TOTK_QUEST_NAME) return 0;
    memcpy(out, row + lp, len); out[len] = 0;
    return 1;
}
static int is_xyz(const TotkBymlVal *v) {
    return v && v->type == TOTK_BYML_DICT && totk_byml_get(v, "X") && totk_byml_get(v, "Y") && totk_byml_get(v, "Z");
}
/* The first dictionary value under key, searched breadth-first a few levels down (the entry itself first). */
static const TotkBymlVal *find(const TotkBymlVal *v, const char *key, int depth) {
    if (!v || depth > 3) return NULL;
    const TotkBymlVal *hit = totk_byml_get(v, key);
    if (hit) return hit;
    if (v->type != TOTK_BYML_DICT && v->type != TOTK_BYML_ARR) return NULL;
    for (size_t i = 0; i < v->n; i++)
        if ((hit = find(&v->items[i], key, depth + 1))) return hit;
    return NULL;
}
static int dest_of(const TotkBymlVal *entry, TotkQuestDest *out) {
    const TotkBymlVal *pos = find(entry, "Pos", 0);
    if (!is_xyz(pos)) pos = find(entry, "AlternativePos", 0);
    if (!is_xyz(pos)) return 0;
    double x = totk_byml_num(totk_byml_get(pos, "X"), NAN), y = totk_byml_num(totk_byml_get(pos, "Y"), NAN),
           z = totk_byml_num(totk_byml_get(pos, "Z"), NAN);
    if (!isfinite(x) || !isfinite(y) || !isfinite(z)) return 0;
    const TotkBymlVal *flag = find(entry, "BlackOutFlag", 0);
    out->x = (float)x; out->y = (float)y; out->z = (float)z;
    out->blackout = flag && flag->type == TOTK_BYML_STR && flag->s[0] ? companion_hash(flag->s) : 0;
    return 1;
}
static int category_of(const TotkBymlVal *row) {
    const TotkBymlVal *c = totk_byml_get(row, "Category");
    if (!c || c->type != TOTK_BYML_STR) return TOTK_QUEST_SIDE;
    return !strcmp(c->s, "Main") ? TOTK_QUEST_MAIN : !strcmp(c->s, "Sub") ? TOTK_QUEST_SHRINE
           : !strcmp(c->s, "ImportantMini") ? TOTK_QUEST_ADVENTURE : TOTK_QUEST_SIDE;
}

void totk_quest_catalog_free(TotkQuestCatalog *c) {
    if (!c) return;
    free(c->quests); free(c->steps); free(c->dests);
    memset(c, 0, sizeof *c);
}

int totk_quest_catalog(const uint8_t *byml, size_t n, TotkQuestCatalog *out) {
    memset(out, 0, sizeof *out);
    TotkBymlVal *root = byml ? totk_byml(byml, n) : NULL;
    if (!root || root->type != TOTK_BYML_ARR) { totk_byml_free(root); return -1; }
    size_t nsteps = 0, ndests = 0;   /* upper bounds first: one allocation each */
    for (size_t r = 0; r < root->n; r++) {
        const TotkBymlVal *steps = totk_byml_get(&root->items[r], "Steps");
        if (!steps || steps->type != TOTK_BYML_ARR) continue;
        nsteps += steps->n;
        for (size_t s = 0; s < steps->n; s++) {
            const TotkBymlVal *d = totk_byml_get(&steps->items[s], "DestinationPoint");
            if (d && d->type == TOTK_BYML_ARR) ndests += d->n;
        }
    }
    out->quests = calloc(root->n ? root->n : 1, sizeof *out->quests);
    out->steps = calloc(nsteps ? nsteps : 1, sizeof *out->steps);
    out->dests = calloc(ndests ? ndests : 1, sizeof *out->dests);
    if (!out->quests || !out->steps || !out->dests) { totk_quest_catalog_free(out); totk_byml_free(root); return -1; }
    for (size_t r = 0; r < root->n; r++) {
        const TotkBymlVal *row = &root->items[r], *id = totk_byml_get(row, "__RowId"), *steps = totk_byml_get(row, "Steps");
        TotkQuest *q = &out->quests[out->nquests];
        if (!id || id->type != TOTK_BYML_STR || !row_name(id->s, q->name) || !steps || steps->type != TOTK_BYML_ARR ||
            !steps->n)
            continue;
        char flag[TOTK_QUEST_NAME + 8];
        snprintf(flag, sizeof flag, "Step_%s", q->name);
        q->name_hash = companion_hash(q->name);
        q->step_flag = companion_hash(flag);
        q->category = category_of(row);
        const TotkBymlVal *sort = totk_byml_get(row, "SortIndex"), *listed = totk_byml_get(row, "IsListAdventureLog");
        q->sort = sort && (sort->type == TOTK_BYML_INT || sort->type == TOTK_BYML_UINT) ? (int)totk_byml_num(sort, 0)
                                                                                         : TOTK_QUEST_NO_SORT;
        q->listed = listed && listed->type == TOTK_BYML_BOOL ? (int)listed->i : 1;
        q->first_step = out->nsteps;
        for (size_t s = 0; s < steps->n; s++) {
            const TotkBymlVal *st = &steps->items[s], *name = totk_byml_get(st, "Name");
            if (!name || name->type != TOTK_BYML_STR || strlen(name->s) >= TOTK_QUEST_STEP_NAME) continue;
            TotkQuestStep *step = &out->steps[out->nsteps++];
            snprintf(step->name, sizeof step->name, "%s", name->s);
            step->hash = companion_hash(step->name);
            const TotkBymlVal *nc = totk_byml_get(st, "IsNotChangeAdventureLog");
            step->not_change_log = nc && nc->type == TOTK_BYML_BOOL && nc->i;
            step->first_dest = out->ndests;
            const TotkBymlVal *d = totk_byml_get(st, "DestinationPoint");
            for (size_t k = 0; d && d->type == TOTK_BYML_ARR && k < d->n; k++)
                if (dest_of(&d->items[k], &out->dests[out->ndests])) out->ndests++;
            step->ndest = out->ndests - step->first_dest;
        }
        q->nsteps = out->nsteps - q->first_step;
        if (q->nsteps) out->nquests++;
    }
    totk_byml_free(root);
    if (!out->nquests) { totk_quest_catalog_free(out); return -1; }
    return 0;
}

int totk_quest_step(const TotkQuestCatalog *c, uint32_t q, uint32_t state_hash) {
    const uint32_t not_ready = companion_hash("NotReady"), complete = companion_hash("Complete");
    if (q >= c->nquests || !state_hash) return TOTK_QUEST_UNKNOWN;
    const TotkQuest *quest = &c->quests[q];
    if (state_hash == not_ready) return TOTK_QUEST_NOT_READY;
    if (state_hash == complete) return (int)quest->nsteps;
    for (uint32_t s = 0; s < quest->nsteps; s++)
        if (c->steps[quest->first_step + s].hash == state_hash) return (int)s;
    return TOTK_QUEST_UNKNOWN;
}
int totk_quest_open(const TotkQuestCatalog *c, uint32_t q, int step) {
    return q < c->nquests && c->quests[q].listed && step >= 1 && step < (int)c->quests[q].nsteps;
}

/* Of the open quests in category cat: the one changed last, else the lowest SortIndex (ties: catalogue order). */
static int best(const TotkQuestCatalog *c, const int *step, const uint64_t *changed, int cat) {
    int by_change = -1, by_sort = -1;
    for (uint32_t q = 0; q < c->nquests; q++) {
        if (c->quests[q].category != cat || !totk_quest_open(c, q, step[q])) continue;
        if (changed && changed[q] && (by_change < 0 || changed[q] > changed[by_change])) by_change = (int)q;
        if (by_sort < 0 || c->quests[q].sort < c->quests[by_sort].sort) by_sort = (int)q;
    }
    return by_change >= 0 ? by_change : by_sort;
}
void totk_quest_pick(const TotkQuestCatalog *c, const int *step, uint32_t selected, const uint64_t *changed, int out[2],
                     int tracked[2]) {
    int t = -1;
    for (uint32_t q = 0; selected && q < c->nquests && t < 0; q++)
        if (c->quests[q].name_hash == selected && totk_quest_open(c, q, step[q])) t = (int)q;
    const int t_main = t >= 0 && c->quests[t].category == TOTK_QUEST_MAIN;
    out[0] = t_main ? t : best(c, step, changed, TOTK_QUEST_MAIN);
    out[1] = t >= 0 && !t_main ? t : best(c, step, changed, TOTK_QUEST_ADVENTURE);
    if (out[1] < 0) out[1] = best(c, step, changed, TOTK_QUEST_SIDE);
    tracked[0] = out[0] >= 0 && out[0] == t;
    tracked[1] = out[1] >= 0 && out[1] == t;
}

double totk_quest_distance(const TotkQuestCatalog *c, uint32_t q, int step, float px, float pz,
                           int (*blackout)(void *ctx, uint32_t hash), void *ctx) {
    if (q >= c->nquests || step < 0 || step >= (int)c->quests[q].nsteps || !isfinite(px) || !isfinite(pz)) return -1;
    const TotkQuestStep *s = &c->steps[c->quests[q].first_step + (uint32_t)step];
    double best_d = -1;
    for (uint32_t k = 0; k < s->ndest; k++) {
        const TotkQuestDest *d = &c->dests[s->first_dest + k];
        if (d->blackout && blackout && blackout(ctx, d->blackout) == 1) continue;   /* the game hides it while set */
        double dx = (double)d->x - px, dz = (double)d->z - pz, dist = sqrt(dx * dx + dz * dz);
        if (isfinite(dist) && (best_d < 0 || dist < best_d)) best_d = dist;
    }
    return best_d;
}
