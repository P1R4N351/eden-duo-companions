/* totk_gamedata.c: GameData values the Eden Duo page needs beyond companion_snapshot.c (MIT, LICENSE.MIT). */
#pragma once
#include "companion.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { TOTK_SHRINES = 152 };   /* DungeonState.Dungeon000 .. Dungeon151 */
typedef struct {
    int stamina_ok;            /* PlayerStatus.Stamina and PlayerStatus.MaxStamina read */
    float stamina, max_stamina;
    int tired_ok, tired;       /* PlayerStatus.IsTired (stamina ran out; regenerating) */
    int shrines_ok;            /* every DungeonState.DungeonNNN enum read */
    int shrines_found;         /* states Open, Enter or Clear */
    int shrines_clear;         /* state Clear */
} TotkGameData;
/* Reads the values above (memory reads only). Call only after companion_snapshot reported "ready" for this main_base:
   that is the check that main is the 1.4.3 build whose GameDataMgr slot this uses. Returns 0 when the manager is not
   there; fields that could not be read keep their _ok at 0. with_shrines: also read the 152 shrine states (about 450
   reads; the module does it on its slower full samples). */
int totk_gamedata_read(uintptr_t main_base, companion_read_fn rd, int with_shrines, TotkGameData *out);

/* SelectChallengeHash: companion_hash of the tracked quest's name, 0 when none (UInt). */
#define TOTK_SELECT_CHALLENGE_HASH 0x2029d3d8u
/* The quest state (memory reads only, same guard as above): out_state[i] = the Enum value of the flag step_flags[i]
   (companion_hash("Step_<Name>")), that is companion_hash of a step name, of "NotReady" or of "Complete"; 0 when the
   flag could not be read. *out_selected: SelectChallengeHash. Returns 0 when the manager is not there (all 0). About
   three reads per flag: the module reads every quest on its full samples (2 Hz). */
int totk_quest_state(uintptr_t main_base, companion_read_fn rd, const uint32_t *step_flags, int n, uint32_t *out_state,
                     uint32_t *out_selected);
/* Bool flags by full hash: out[i] 1 or 0, -1 when there is no such Bool (a BoolExp, an unknown name). Returns 0 when
   the manager is not there (all -1). */
int totk_gamedata_bools(uintptr_t main_base, companion_read_fn rd, const uint32_t *hashes, int n, signed char *out);
#ifdef __cplusplus
}
#endif
