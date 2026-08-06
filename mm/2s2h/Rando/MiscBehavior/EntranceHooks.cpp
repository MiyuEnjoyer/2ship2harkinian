#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"
#include "2s2h/Rando/Logic/EntranceShuffle.h"

extern "C" {
#include "functions.h"
#include "variables.h"
#include "z64scene.h"
}

namespace Rando {

namespace EntranceShuffle {

static bool sPlayerSceneExitPending = false;

static RegisterShipInitFunc registerHooks(
    []() {
        COND_HOOK(OnPlayerSceneExit, IsEntranceShuffleEnabled(),
                  [](s32 nextEntrance) { sPlayerSceneExitPending = true; });

        // Ikana's weather follows you through an entrance and swaps the scene over to the tinted Ikana sky, but a
        // scene with no sky of its own has nothing to draw it with and crashes. (only possible with entrance rando)
        COND_HOOK(OnSceneInit, IsEntranceShuffleEnabled(), [](s8 sceneId, s8 spawnNum) {
            if (gPlayState->skyboxCtx.dListBuf == NULL) {
                gPlayState->skyboxId = SKYBOX_NONE;
            }
        });

        COND_HOOK(OnPlayDestroy, IsEntranceShuffleEnabled(), []() {
            bool playerWalkedThroughIt = sPlayerSceneExitPending;
            sPlayerSceneExitPending = false;

            // Grottos (and the Lone Peak Shrine, which shares the KAKUSIANA scene) always send the player
            // back to their scene's default spawn, which the game also reuses for overworld arrivals. None
            // of those entrances are shuffled, so remapping one here would fling the exit to a shuffled
            // destination and crash / drop the player through the world. Leaving that scene stays vanilla.
            if (gPlayState != NULL && gPlayState->sceneId == SCENE_KAKUSIANA) {
                return;
            }

            s32 originalEntrance = gSaveContext.save.entrance;
            s32 shuffledEntrance;

            if (playerWalkedThroughIt) {
                shuffledEntrance = GetShuffledEntrance(originalEntrance);
            } else if (originalEntrance == ENTRANCE(SOUTH_CLOCK_TOWN, 0) && gSaveContext.respawnFlag == 0) {
                shuffledEntrance = GetStartEntrance();
            } else {
                return;
            }

            if (shuffledEntrance != originalEntrance) {
                gSaveContext.save.entrance = shuffledEntrance;

                // gSaveContext.nextCutsceneIndex is set independently of the entrance by dozens of call
                // sites (boss-defeat warps, credits sequences, NPC cutscenes) that pair one specific
                // vanilla entrance with one specific day/cutscene-variant index meant for that exact
                // destination. We just redirected the entrance to somewhere unrelated, so a leftover
                // index here is stale - and dangerous: Play_Init folds it into gSaveContext.sceneLayer,
                // which gets added directly into the entrance value passed to Entrance_GetTableEntry
                // (z_scene_table.c), an unchecked index into that spawn's per-layer-variant entry array.
                // Most spawns define only one such entry, so a leaked index reads garbage past the end of
                // that array as if it were a real {sceneId, spawnNum, flags}. Clearing it lets the new
                // scene's own Play_Init recompute sceneLayer fresh from its own state, which is always
                // safe (0, or a correct in-bounds value for the few scenes whose tables are sized for it).
                gSaveContext.nextCutsceneIndex = 0xFFEF;
            }
        });
    },
    { "IS_RANDO" });

} // namespace EntranceShuffle

} // namespace Rando
