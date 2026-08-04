#include <libultraship/bridge/consolevariablebridge.h>
#include "2s2h/Enhancements/Enhancements.h"
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"
#include "2s2h/ShipUtils.h"
#include "2s2h/ObjectExtension/ActorListIndex.h"
#include "2s2h/Rando/Rando.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <map>
#include <set>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

extern "C" {
#include "variables.h"
#include "functions.h"
#include "overlays/actors/ovl_En_Firefly/z_en_firefly.h"
#include "overlays/actors/ovl_En_Slime/z_en_slime.h"
#include "overlays/actors/ovl_En_Tite/z_en_tite.h"
#include "overlays/actors/ovl_En_Ik/z_en_ik.h"
#include "overlays/actors/ovl_En_Dekubaba/z_en_dekubaba.h"
#include "overlays/actors/ovl_En_Rat/z_en_rat.h"
#include "overlays/actors/ovl_En_Rd/z_en_rd.h"
#include "overlays/actors/ovl_En_Snowman/z_en_snowman.h"
#include "overlays/actors/ovl_En_Karebaba/z_en_karebaba.h"
#include "overlays/actors/ovl_En_Pp/z_en_pp.h"
#include "overlays/actors/ovl_En_Rr/z_en_rr.h"
#include "overlays/actors/ovl_En_Jso2/z_en_jso2.h"
#include "overlays/actors/ovl_En_Bigpo/z_en_bigpo.h"
#include "overlays/actors/ovl_En_Wallmas/z_en_wallmas.h"
#include "overlays/actors/ovl_En_Famos/z_en_famos.h"
#include "overlays/actors/ovl_En_Okuta/z_en_okuta.h"
#include "overlays/actors/ovl_En_Bee/z_en_bee.h"
#include "overlays/actors/ovl_En_Box/z_en_box.h"
#include "overlays/actors/ovl_Door_Shutter/z_door_shutter.h"
#include "overlays/actors/ovl_En_Dekunuts/z_en_dekunuts.h"
#include "overlays/actors/ovl_En_Elforg/z_en_elforg.h"
#include "overlays/actors/ovl_Obj_Etcetera/z_obj_etcetera.h"
}

#define CVAR_MODE_NAME "gModes.EnemyRando.Mode"
#define CVAR_MODE CVarGetInteger(CVAR_MODE_NAME, ENEMY_RANDO_OFF)

enum EnemySpawnFlags {
    // Respawns or splits, so it never leaves the enemy list for good.
    ENEMY_SPAWN_RENEWABLE = 1 << 0,
    // Happy at whatever height the vanilla actor was placed at; everything else gets dropped onto the
    // floor beneath that spot.
    ENEMY_SPAWN_AIRBORNE = 1 << 1,
    // Only joins ACTORCAT_ENEMY once the player comes near, so a room counting that list can register
    // as clear before this has been fought.
    ENEMY_SPAWN_DORMANT = 1 << 2,
    // Unusable wherever something waits on this exact enemy dying.
    ENEMY_SPAWN_UNRELIABLE_DEATH = ENEMY_SPAWN_RENEWABLE | ENEMY_SPAWN_DORMANT,
};

// Actor entries pack flags into the high bits of their id.
static constexpr s16 ACTOR_ID_MASK = 0x1FFF;

struct EnemySpawnEntry {
    s16 actorId;
    s16 params;
    const char* name;
    u8 flags = 0;
};

static const std::vector<EnemySpawnEntry> sEnemyPool = {
    { ACTOR_EN_FIREFLY, KEESE_FIRE_FLY, "Fire Keese", ENEMY_SPAWN_AIRBORNE },
    { ACTOR_EN_FIREFLY, KEESE_NORMAL_FLY, "Keese", ENEMY_SPAWN_AIRBORNE },
    { ACTOR_EN_FIREFLY, KEESE_ICE_FLY, "Ice Keese", ENEMY_SPAWN_AIRBORNE },

    { ACTOR_EN_SLIME, EN_SLIME_TYPE_BLUE, "Blue ChuChu", ENEMY_SPAWN_RENEWABLE },
    { ACTOR_EN_SLIME, EN_SLIME_TYPE_GREEN, "Green ChuChu", ENEMY_SPAWN_RENEWABLE },
    { ACTOR_EN_SLIME, EN_SLIME_TYPE_YELLOW, "Yellow ChuChu", ENEMY_SPAWN_RENEWABLE },
    { ACTOR_EN_SLIME, EN_SLIME_TYPE_RED, "Red ChuChu", ENEMY_SPAWN_RENEWABLE },

    { ACTOR_EN_TITE, ENTITE_MINUS_0, "Tektite" },
    { ACTOR_EN_TITE, ENTITE_MINUS_2, "Blue Tektite" },

    { ACTOR_EN_IK, IK_TYPE_SILVER, "Silver Iron Knuckle" },
    { ACTOR_EN_IK, IK_TYPE_BLACK, "Black Iron Knuckle" },
    { ACTOR_EN_IK, IK_TYPE_WHITE, "White Iron Knuckle" },

    { ACTOR_EN_WF, 0, "Wolfos" },
    { ACTOR_EN_WF, 1, "White Wolfos" },

    { ACTOR_EN_DEKUBABA, DEKUBABA_NORMAL, "Deku Baba" },
    { ACTOR_EN_DEKUBABA, DEKUBABA_BIG, "Big Deku Baba" },

    { ACTOR_EN_RAT, EN_RAT_TYPE_DUNGEON, "Real Bombchu", ENEMY_SPAWN_RENEWABLE },
    { ACTOR_EN_RAT, static_cast<s16>(0x8000), "Real Bombchu (Overworld)", ENEMY_SPAWN_RENEWABLE },

    { ACTOR_EN_RD, EN_RD_TYPE_GIBDO, "Gibdo" },
    { ACTOR_EN_RD, EN_RD_TYPE_REGULAR, "ReDead" },

    { ACTOR_EN_SNOWMAN, EN_SNOWMAN_TYPE_SMALL, "Eeno" },
    { ACTOR_EN_SNOWMAN, EN_SNOWMAN_TYPE_LARGE, "Large Eeno" },

    { ACTOR_EN_MKK, 0, "Black Boe" },
    { ACTOR_EN_MKK, 1, "White Boe" },

    { ACTOR_EN_KAREBABA, ENKAREBABA_1, "Wilted Deku Baba" },
    { ACTOR_EN_KAREBABA, KAREBABA_MINI, "Mini Baba", ENEMY_SPAWN_RENEWABLE },

    { ACTOR_EN_NEO_REEBA, 0, "Leever" },
    { ACTOR_EN_NEO_REEBA, static_cast<s16>(0x8000), "Large Leever" },

    { ACTOR_EN_PP, EN_PP_TYPE_MASKED, "Hiploop (Masked)" },
    { ACTOR_EN_PP, EN_PP_TYPE_NO_MASK, "Hiploop (No Mask)" },
    { ACTOR_EN_PP, EN_PP_TYPE_UNMASKED, "Hiploop (Unmasked)" },

    { ACTOR_EN_PEEHAT, 0, "Peahat", ENEMY_SPAWN_AIRBORNE },
    { ACTOR_EN_PEEHAT, 1, "Peahat Larva", ENEMY_SPAWN_AIRBORNE },

    // Replaces gold skulltulas too.. figure out something else for this
    // { ACTOR_EN_SW, 0, "Skullwalltula" },

    { ACTOR_EN_DODONGO, 0, "Dodongo" },
    { ACTOR_EN_DODONGO, 1, "Large Dodongo" },

    { ACTOR_EN_RR, LIKE_LIKE_PARAM_1, "Like Like" },
    { ACTOR_EN_RR, LIKE_LIKE_PARAM_3, "Giant Like Like" },

    // Disabled for now, maybe needs some reduced chance of spawning or something
    // { ACTOR_EN_JSO2, EN_JSO2_TYPE_NORMAL, "Garo Master" },

    { ACTOR_EN_BIGPO, BIG_POE_TYPE_REGULAR, "Big Poe", ENEMY_SPAWN_AIRBORNE },

    { ACTOR_EN_AM, 0, "Armos" },
    { ACTOR_EN_BAGUO, 0, "Nejiron" },
    { ACTOR_EN_BAT, 0, "Bad Bat", ENEMY_SPAWN_AIRBORNE },
    { ACTOR_EN_BB, 0, "Blue Bubble", ENEMY_SPAWN_RENEWABLE | ENEMY_SPAWN_AIRBORNE },
    { ACTOR_EN_BBFALL, 0, "Red Bubble", ENEMY_SPAWN_AIRBORNE },
    { ACTOR_EN_BEE, BEE_BEHAVIOR_ATTACK, "Giant Bee", ENEMY_SPAWN_AIRBORNE },
    { ACTOR_EN_CROW, 0, "Guay", ENEMY_SPAWN_RENEWABLE | ENEMY_SPAWN_AIRBORNE },
    { ACTOR_EN_DEKUNUTS, 0, "Mad Scrub" },
    { ACTOR_EN_DINOFOS, 0, "Dinolfos" },
    { ACTOR_EN_FAMOS, FAMOS_PATH_INDEX_NONE, "Death Armos" },
    { ACTOR_EN_FLOORMAS, 0, "Floormaster" },
    { ACTOR_EN_FZ, 0, "Freezard" },
    { ACTOR_EN_GRASSHOPPER, 0, "Dragonfly", ENEMY_SPAWN_AIRBORNE },
    { ACTOR_EN_KAME, 0, "Snapper" },
    { ACTOR_EN_POH, 0, "Poe", ENEMY_SPAWN_AIRBORNE },
    // These don't seem to be damageable
    // { ACTOR_EN_PR, 0, "Desbreko", ENEMY_SPAWN_AIRBORNE },
    // { ACTOR_EN_SB, 0, "Shellblade" },
    { ACTOR_EN_SKB, 0, "Stalchild" },
    { ACTOR_EN_VM, 0, "Beamos" },
    // Annoying, maybe this will be re-added with a menu
    // { ACTOR_EN_WALLMAS, WALLMASTER_TYPE_TIMER_ONLY, "Wallmaster" },
    { ACTOR_EN_WDHAND, 0, "Dexihand" },
    // Need some water safe tagging or something, also need the ones in great bay temple to be excluded
    // { ACTOR_EN_OKUTA, EN_OKUTA_TYPE_RED_OCTOROK, "Octorok" },
    { ACTOR_EN_ST, 0, "Large Skulltula", ENEMY_SPAWN_AIRBORNE },
    { ACTOR_EN_THIEFBIRD, 0, "Takkuri", ENEMY_SPAWN_AIRBORNE },
    { ACTOR_EN_TUBO_TRAP, 0, "Flying Pot Trap", ENEMY_SPAWN_DORMANT },
};

static const std::unordered_set<s16> sRandomizableActorIds = [] {
    std::unordered_set<s16> ids;
    for (const EnemySpawnEntry& entry : sEnemyPool) {
        ids.insert(entry.actorId);
    }
    return ids;
}();

// Blacklist: actor IDs excluded from the pool at a specific (scene, room, actor list index) placement,
// for enemies that break that particular vanilla spot (e.g. no room for a Large Skulltula to hang).
static const std::map<std::tuple<u16, u8, s16>, std::vector<s16>> sBlacklistedActorsByLocation = {
    // { { SCENE_EXAMPLE, 0, 3 }, { ACTOR_EN_ST } },
};

// Whitelist: actor IDs allowed to randomize at a specific (scene, room) despite being spawned
// dynamically (GetActorListIndex == -1). Only whitelist a verified one-off spawn with no bookkeeping
// tied to its specific death/instance, or it can reintroduce the multiplying bug (or worse).
static const std::map<std::tuple<u16, u8, s16>, std::vector<s16>> sWhitelistedDynamicActorsByLocation = {
    // { { SCENE_EXAMPLE, 0, -1 }, { ACTOR_EN_EXAMPLE } },

    // Captain Keeta's Stalchildren won't work here
};

// Rooms where nothing is randomized at all. This disables the mode for the whole room, so prefer
// teaching the mechanisms below about the individual spot.
static const std::set<std::pair<s16, s8>> sExcludedRooms = {
    // { SCENE_EXAMPLE, 0 },
};

static bool IsExcludedRoom(s16 sceneId, s8 room) {
    return sExcludedRooms.count({ sceneId, room }) != 0;
}

// Killing a room's last ACTORCAT_ENEMY actor only sets the temporary clear flag; these are the actors
// that promote it to the permanent one with Flags_SetClear, which is what everything else waits on.
static bool IsRoomClearPromoter(s16 actorId) {
    switch (actorId) {
        case ACTOR_DOOR_SHUTTER:
        case ACTOR_OBJ_ROOMTIMER:
        case ACTOR_BG_IKANA_SHUTTER:
            return true;
        default:
            return false;
    }
}

static bool IsRoomClearChest(ActorEntry* entry) {
    return ((entry->id & ACTOR_ID_MASK) == ACTOR_EN_BOX) && ((ENBOX_GET_TYPE(entry) == ENBOX_TYPE_BIG_ROOM_CLEAR) ||
                                                             (ENBOX_GET_TYPE(entry) == ENBOX_TYPE_SMALL_ROOM_CLEAR));
}

// Shutters are transition actors rather than room actors, so the room list alone misses them. Most
// aren't waiting on enemies: DoorShutter_SetupDoor only installs the room-clear action for an effective
// DOORSHUTTER_TYPE_1, which func_808A0900 ("is the door's own room the front room") decides - a declared
// type 1 collapses to type 0 on the front side, while types 6 and 7 collapse into type 1 there.
static bool TransitionPromotesRoomClear(TransitionActorEntry* entry, s8 room) {
    // Actor_SpawnTransitionActors has already run and negated the id of everything it spawned.
    s16 actorId = ABS_ALT(entry->id) & ACTOR_ID_MASK;

    if (actorId == ACTOR_DOOR_SHUTTER) {
        switch (DOORSHUTTER_PARAMS_GET_TYPE(entry->params)) {
            case DOORSHUTTER_TYPE_1:
                return entry->sides[1].room == room;
            case DOORSHUTTER_TYPE_6:
            case DOORSHUTTER_TYPE_7:
                return entry->sides[0].room == room;
            default:
                return false;
        }
    }

    return IsRoomClearPromoter(actorId) && ((entry->sides[0].room == room) || (entry->sides[1].room == room));
}

static bool ComputeIsRoomKillGated(s8 room) {
    ActorEntry* entries = gPlayState->setupActorList;

    for (s32 i = 0; (entries != NULL) && (i < gPlayState->numSetupActors); i++) {
        if (IsRoomClearPromoter(entries[i].id & ACTOR_ID_MASK) || IsRoomClearChest(&entries[i])) {
            return true;
        }
    }

    TransitionActorEntry* transitions = gPlayState->transitionActors.list;

    for (s32 i = 0; (transitions != NULL) && (i < gPlayState->transitionActors.count); i++) {
        if (TransitionPromotesRoomClear(&transitions[i], room)) {
            return true;
        }
    }

    return false;
}

// Vanilla pairs a trapped Stray Fairy with its host by placing both at identical coordinates (see
// EnElforg_GetHoldingEnemy). The fairy is only freed when that exact actor dies.
static std::set<s16> ComputeStrayFairyHostSpots() {
    std::set<s16> hosts;
    ActorEntry* entries = gPlayState->setupActorList;
    if (entries == NULL) {
        return hosts;
    }

    std::vector<Vec3s> fairyPositions;
    for (s32 i = 0; i < gPlayState->numSetupActors; i++) {
        if (((entries[i].id & ACTOR_ID_MASK) == ACTOR_EN_ELFORG) &&
            (STRAY_FAIRY_TYPE(&entries[i]) == STRAY_FAIRY_TYPE_ENEMY)) {
            fairyPositions.push_back(entries[i].pos);
        }
    }

    if (fairyPositions.empty()) {
        return hosts;
    }

    for (s32 i = 0; i < gPlayState->numSetupActors; i++) {
        for (const Vec3s& fairyPos : fairyPositions) {
            if ((entries[i].pos.x == fairyPos.x) && (entries[i].pos.y == fairyPos.y) &&
                (entries[i].pos.z == fairyPos.z) && ((entries[i].id & ACTOR_ID_MASK) != ACTOR_EN_ELFORG)) {
                hosts.insert(i);
                break;
            }
        }
    }

    return hosts;
}

// Worked out once per room load rather than re-derived per spawn.
struct RoomEnemyRules {
    bool killGated = false;
    std::set<s16> strayFairyHostIndices;
};

static std::map<std::pair<s16, s8>, RoomEnemyRules> sRoomRules;

static const RoomEnemyRules* GetRoomRules(s16 sceneId, s8 room) {
    auto it = sRoomRules.find({ sceneId, room });
    return it != sRoomRules.end() ? &it->second : NULL;
}

// Counting and then indexing keeps this to a single Ship_Random draw, so seeded mode keeps picking the
// same enemy for a spot.
static const EnemySpawnEntry* PickReplacement(s16 sceneId, s8 room, s16 actorListIndex, bool needsReliableDeath) {
    auto blacklist = sBlacklistedActorsByLocation.find({ (u16)sceneId, (u8)room, actorListIndex });
    bool hasLocationBlacklist = blacklist != sBlacklistedActorsByLocation.end();
    const RoomEnemyRules* rules = GetRoomRules(sceneId, room);
    bool requiresReliableDeath =
        needsReliableDeath ||
        ((rules != NULL) && (rules->killGated || (rules->strayFairyHostIndices.count(actorListIndex) != 0)));

    auto isEligible = [&](const EnemySpawnEntry& entry) {
        if (requiresReliableDeath && (entry.flags & ENEMY_SPAWN_UNRELIABLE_DEATH)) {
            return false;
        }
        return !hasLocationBlacklist || (std::find(blacklist->second.begin(), blacklist->second.end(), entry.actorId) ==
                                         blacklist->second.end());
    };

    if (!hasLocationBlacklist && !requiresReliableDeath) {
        return &sEnemyPool[Ship_Random(0, static_cast<s32>(sEnemyPool.size()))];
    }

    s32 eligibleCount = 0;
    for (const EnemySpawnEntry& entry : sEnemyPool) {
        if (isEligible(entry)) {
            eligibleCount++;
        }
    }

    if (eligibleCount == 0) {
        return NULL;
    }

    s32 pick = Ship_Random(0, eligibleCount);
    for (const EnemySpawnEntry& entry : sEnemyPool) {
        if (isEligible(entry) && (pick-- == 0)) {
            return &entry;
        }
    }

    return NULL;
}

static bool IsWhitelistedDynamicActor(s16 sceneId, s8 room, s16 actorId) {
    auto it = sWhitelistedDynamicActorsByLocation.find({ (u16)sceneId, (u8)room, static_cast<s16>(-1) });
    return it != sWhitelistedDynamicActorsByLocation.end() &&
           std::find(it->second.begin(), it->second.end(), actorId) != it->second.end();
}

// A Keese's spot sits well off the ground, and most ground enemies never apply gravity of their own,
// so without this they hover there forever. The ray starts slightly above the spot so an enemy already
// sitting flush on - or a little inside - the floor still finds it, same as EnKusa_SnapToFloor.
static void EnemyRando_SnapSpawnPosToFloor(Vec3f* pos) {
    CollisionPoly* poly;
    Vec3f rayStart = { pos->x, pos->y + 30.0f, pos->z };

    f32 floorY = BgCheck_EntityRaycastFloor1(&gPlayState->colCtx, &poly, &rayStart);
    if (floorY > BGCHECK_Y_MIN) {
        pos->y = floorY;
    }
}

// Not every scene-placed instance of a pool actor is something to swap. Both of these actors mask
// params down to the low byte in their Init, which is where their type lives.
static bool ShouldSkipAtSpawn(Actor* actor) {
    switch (actor->id) {
        case ACTOR_EN_DEKUNUTS:
            // The Deku Shrine's scrubs. Type 1 clears the collider's material and marks it
            // ACELEM_NO_DAMAGE, so it can never be killed - it only ducks in and out of its flower.
            return (actor->params & 0xFF) == ENDEKUNUTS_GET_FF00_1;
        case ACTOR_EN_BIGPO:
            // Dampe's grave hides Big Poes as the flames he digs up, and summons one more for the
            // fight at the end of that hunt (see VB_BIG_POE_APPEAR_AFTER_FLAME_HUNT). Only the well
            // poe is swapped here.
            switch (actor->params & 0xFF) {
                case BIG_POE_TYPE_SUMMONED:
                case BIG_POE_TYPE_POSSIBLE_FIRE:
                case BIG_POE_TYPE_CHOSEN_FIRE:
                case BIG_POE_TYPE_REVEALED_FIRE:
                case BIG_POE_TYPE_UNK5:
                    return true;
                default:
                    return false;
            }
        default:
            return false;
    }
}

static HOOK_ID sSummonedPoeDeathHookId = 0;

static void EnemyRando_InitRandomSeed(Actor* actor, s16 actorListIndex) {
    uint64_t baseSeed =
        IS_RANDO ? gSaveContext.save.shipSaveInfo.rando.finalSeed : gSaveContext.save.shipSaveInfo.fileCreatedAt;
    uint64_t seed = baseSeed + (static_cast<uint64_t>(gPlayState->sceneId) * 7919) +
                    (static_cast<uint64_t>(actor->room) * 104729) +
                    (static_cast<uint64_t>(static_cast<u16>(actorListIndex)) * 1299709) +
                    (static_cast<uint64_t>(actor->id) * 15485867);
    Ship_Random_Seed(seed);
}

// Returns the spawned actor, or NULL if nothing was eligible. `needsReliableDeath` is for spots where
// something waits on this exact enemy dying rather than on the room as a whole.
static Actor* EnemyRando_SpawnReplacement(Actor* actor, s16 actorListIndex, bool needsReliableDeath) {
    if (CVAR_MODE == ENEMY_RANDO_RANDOM_SEEDED) {
        EnemyRando_InitRandomSeed(actor, actorListIndex);
    }

    const EnemySpawnEntry* replacement =
        PickReplacement(gPlayState->sceneId, actor->room, actorListIndex, needsReliableDeath);
    if (replacement == NULL) {
        return NULL;
    }

    Vec3f spawnPos = actor->world.pos;
    if (!(replacement->flags & ENEMY_SPAWN_AIRBORNE)) {
        EnemyRando_SnapSpawnPosToFloor(&spawnPos);
    }

    SPDLOG_INFO("EnemyRando: replacing {} with {} ({} room {})", GetActorDescription(actor->id), replacement->name,
                Ship_GetSceneName(gPlayState->sceneId), actor->room);

    return Actor_Spawn(&gPlayState->actorCtx, gPlayState, replacement->actorId, spawnPos.x, spawnPos.y, spawnPos.z,
                       actor->shape.rot.x, actor->shape.rot.y, actor->shape.rot.z, replacement->params);
}

// Stands a randomized enemy in for the Big Poe that Dampe's flame hunt would have summoned. Returns
// false if nothing was eligible, in which case the poe should just appear as it normally would.
static bool EnemyRando_TakeOverFlameHuntFight(EnBigpo* poe) {
    Actor* replacement = EnemyRando_SpawnReplacement(&poe->actor, GetActorListIndex(&poe->actor), true);

    if (replacement == NULL) {
        return false;
    }

    // The bottle chest is waiting on the switch flag the poe would have set as it died, so hand that
    // over to whatever is being fought in its place.
    if (sSummonedPoeDeathHookId != 0) {
        GameInteractor::Instance->UnregisterGameHookForPtr<GameInteractor::OnActorKill>(sSummonedPoeDeathHookId);
    }

    sSummonedPoeDeathHookId = GameInteractor::Instance->RegisterGameHookForPtr<GameInteractor::OnActorKill>(
        (uintptr_t)replacement, [switchFlag = poe->switchFlag](Actor*) {
            if (switchFlag != BIG_POE_SWITCH_FLAG_NONE) {
                Flags_SetSwitch(gPlayState, switchFlag);
            }

            // Unregistering is deferred, so doing it from inside the hook is fine, and it stops a
            // recycled actor slot at this address from tripping it again.
            GameInteractor::Instance->UnregisterGameHookForPtr<GameInteractor::OnActorKill>(sSummonedPoeDeathHookId);
            sSummonedPoeDeathHookId = 0;
        });

    // Without this the poe keeps counting flames and asking again every frame, and the revealed flames
    // burn on forever.
    for (Actor* fire = poe->actor.child; fire != NULL; fire = fire->child) {
        Actor_Kill(fire);
    }
    Actor_Kill(&poe->actor);

    return true;
}

void RegisterEnemyRando() {
    COND_VB_SHOULD(VB_ENABLE_OBJECT_DEPENDENCY, CVAR_MODE, { *should = false; });

    // Match on XZ only, since the replacement may have been dropped onto the floor. Enemies with an
    // init still pending are ignored: the room's enemies are spawned a frame before their init runs,
    // and binding to a vanilla enemy that ShouldActorInit then swaps out frees the fairy for nothing.
    COND_VB_SHOULD(VB_STRAY_FAIRY_IS_HELD_BY_ENEMY, CVAR_MODE, {
        Actor* fairy = va_arg(args, Actor*);
        Actor* enemy = va_arg(args, Actor*);

        *should = (enemy->init == NULL) && (enemy->home.pos.x == fairy->home.pos.x) &&
                  (enemy->home.pos.z == fairy->home.pos.z);
    });

    COND_HOOK(AfterRoomSceneCommands, CVAR_MODE, [](s16 sceneId, s8 roomNum) {
        RoomEnemyRules& rules = sRoomRules[std::make_pair(sceneId, roomNum)];

        rules.killGated = ComputeIsRoomKillGated(roomNum);
        rules.strayFairyHostIndices = ComputeStrayFairyHostSpots();
    });

    COND_HOOK(ShouldActorInit, CVAR_MODE, [](Actor* actor, bool* should) {
        if (!sRandomizableActorIds.count(actor->id) || ShouldSkipAtSpawn(actor) ||
            IsExcludedRoom(gPlayState->sceneId, actor->room)) {
            return;
        }

        // Only replace actors spawned directly from the room's static actor list (GetActorListIndex
        // returns -1 for anything spawned dynamically), unless this exact spot is whitelisted above.
        s16 actorListIndex = GetActorListIndex(actor);
        if (actorListIndex < 0 && !IsWhitelistedDynamicActor(gPlayState->sceneId, actor->room, actor->id)) {
            return;
        }

        *should = false;

        // Mad Scrub always drops a bouncy Deku Flower on death. Spawn
        // it immediately instead of relying on the replacement to ever die.
        if (actor->id == ACTOR_EN_DEKUNUTS) {
            Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_OBJ_ETCETERA, actor->home.pos.x, actor->home.pos.y,
                        actor->home.pos.z, 0, actor->home.rot.y, 0,
                        DEKU_FLOWER_PARAMS(DEKU_FLOWER_TYPE_PINK_WITH_INITIAL_BOUNCE));
        }

        EnemyRando_SpawnReplacement(actor, actorListIndex, false);
    });

    COND_VB_SHOULD(VB_BIG_POE_APPEAR_AFTER_FLAME_HUNT, CVAR_MODE, {
        EnBigpo* poe = va_arg(args, EnBigpo*);

        *should = !EnemyRando_TakeOverFlameHuntFight(poe);
    });
}

static RegisterShipInitFunc initFunc(RegisterEnemyRando, { CVAR_MODE_NAME });
