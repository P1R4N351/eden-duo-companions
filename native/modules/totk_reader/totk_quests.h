/* totk_quests.c: the quest catalogue from the player's romfs and the quest the Ready page's left pager shows (MIT,
   LICENSE.MIT). */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Adventure Log tabs, from the row's Category: Main, Sub (the crystal quests, the game's Shrine Quests tab),
   ImportantMini (Side Adventures), none (Side Quests). */
enum { TOTK_QUEST_MAIN, TOTK_QUEST_SHRINE, TOTK_QUEST_ADVENTURE, TOTK_QUEST_SIDE };
enum { TOTK_QUEST_NAME = 64, TOTK_QUEST_STEP_NAME = 64, TOTK_QUEST_NO_SORT = 0x7fffffff };
typedef struct { float x, y, z; uint32_t blackout; } TotkQuestDest;   /* blackout: companion_hash(BlackOutFlag), 0 none */
typedef struct {
    char name[TOTK_QUEST_STEP_NAME];
    uint32_t hash;                  /* companion_hash(name): the value Step_<Quest> holds while at this step */
    int not_change_log;             /* IsNotChangeAdventureLog */
    uint32_t first_dest, ndest;     /* into TotkQuestCatalog.dests */
} TotkQuestStep;
typedef struct {
    char name[TOTK_QUEST_NAME];     /* <Name> of Work/Challenge/<Name>.game__challenge__Challenge.gyml */
    uint32_t name_hash;             /* companion_hash(name): what SelectChallengeHash holds while it is tracked */
    uint32_t step_flag;             /* companion_hash("Step_" name) */
    int category;                   /* TOTK_QUEST_* */
    int sort;                       /* SortIndex; TOTK_QUEST_NO_SORT when the row has none (31 rows on 1.4.3): last */
    int listed;                     /* IsListAdventureLog (default true) */
    uint32_t first_step, nsteps;    /* into TotkQuestCatalog.steps; step 0 is "Ready" */
} TotkQuest;
typedef struct {
    TotkQuest *quests; uint32_t nquests;
    TotkQuestStep *steps; uint32_t nsteps;
    TotkQuestDest *dests; uint32_t ndests;
} TotkQuestCatalog;
/* The catalogue from RSDB/Challenge.Product.<v>.rstbl.byml (decompressed): 0 on success; -1 when it is not one. Rows
   without a name or steps are skipped. Free with totk_quest_catalog_free. */
int totk_quest_catalog(const uint8_t *byml, size_t n, TotkQuestCatalog *out);
void totk_quest_catalog_free(TotkQuestCatalog *c);

/* Where quest q stands, from its Step_ value: -2 unknown (0, or a value that is none of its steps), -1 NotReady,
   0..nsteps-1 the step index (0 Ready: offered, not taken), nsteps Complete. */
enum { TOTK_QUEST_UNKNOWN = -2, TOTK_QUEST_NOT_READY = -1 };
int totk_quest_step(const TotkQuestCatalog *c, uint32_t q, uint32_t state_hash);
/* In the Adventure Log and still open: listed, past Ready, not Complete. */
int totk_quest_open(const TotkQuestCatalog *c, uint32_t q, int step);

/* The two quest pages of the left pager. step: per quest, from totk_quest_step. selected: SelectChallengeHash.
   changed: per quest, when the module last saw its step change in this session (larger is later, 0 never; may be NULL).
   page 0 (Main quest): the tracked quest if it is a main quest and open, else the open main quest whose step changed
   last, else the open main quest with the lowest SortIndex. page 1 (Side quest): the tracked quest if it is not a main
   quest and open, else among open Side Adventures, then (none open) open Side Quests: the one changed last, else the
   lowest SortIndex. A tracked quest that is Complete (a save can hold one) is not shown. out[p] = quest index or -1;
   tracked[p] = 1 when it is the tracked one. */
void totk_quest_pick(const TotkQuestCatalog *c, const int *step, uint32_t selected, const uint64_t *changed, int out[2],
                     int tracked[2]);
/* The distance in metres on the map plane (x, z) from (px, pz) to the nearest destination of quest q's step, skipping
   destinations whose BlackOutFlag is set: blackout(hash) returns 1 when set, 0 not set, -1 unknown (treated as not
   set; may be NULL). -1 when the step has no destination. */
double totk_quest_distance(const TotkQuestCatalog *c, uint32_t q, int step, float px, float pz,
                           int (*blackout)(void *ctx, uint32_t hash), void *ctx);
#ifdef __cplusplus
}
#endif
