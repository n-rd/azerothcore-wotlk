/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONEVENTTABLES_H
#define _PLAYERBOT_DUNGEONEVENTTABLES_H

#include <unordered_map>
#include <vector>

#include "Ai/Dungeon/DungeonClear/Data/DungeonEventRegistry.h"

class Player;
class Creature;
class AiObjectContext;
struct BossRosterPatch;
struct DungeonWingLayout;

// Internal registration seam for the per-dungeon event tables.
//
// Each dungeon owns one .cpp in this folder that defines its event rows
// (Register<Dungeon>Events). A Conditional event's activation predicate is a
// free function defined in the SAME file, handed to the builder by pointer
// (.Conditional(&MyPredicate)) — there is no separate condition registry and no
// global id space to keep collision-free. The central DungeonEventRegistry calls
// the Register<Dungeon>Events aggregators EXPLICITLY so every per-dungeon
// translation unit stays referenced.
//
// Why explicit calls and not self-registering static initializers: the module
// compiles into a static lib, and a TU whose only output is constructor
// side-effects (with no symbol the program references) is dropped by the linker
// — its events would silently vanish. The one-big-table this replaces avoided
// that by keeping everything in a single referenced TU; the aggregator calls
// below restore the reference chain per file. (Same reason ObjectiveHookRegistry
// and friends use hardcoded tables.)
//
// Adding a dungeon:
//   1. Create <Dungeon>Events.cpp here.
//   2. Define Register<Dungeon>Events; for any Conditional event, define its
//      predicate as a static free function in that file and pass &Predicate to
//      .Conditional() (a typo is a compile error, not a silent never-fire).
//   3. Declare the appender below.
//   4. Add the call in EventTable() (DungeonEventRegistry.cpp).
//   5. If the dungeon corrects the auto-derived boss list, define its roster
//      patch as Register<Dungeon>Roster in the SAME file (using the DcRoster
//      builders in DungeonRosterBuilders.h), declare it below, and add the call
//      in PatchTable() (BossRosterRegistry.cpp). One file owns all of a
//      dungeon's clear data: event rows + conditions + roster patch.

// Shared, cross-dungeon activation predicate — external linkage so several
// dungeon files can pass &DcRoomAggroPreClearCondition to .Conditional().
// DUE while the room-trash value still has anything to clear (every
// RoomAggroRegistry boss: SM Cathedral, Scholomance Marduk & Vectus, ...).
bool DcRoomAggroPreClearCondition(Player* bot, AiObjectContext* context);

// --- event rows (one appender per dungeon) -------------------------------
void RegisterSunkenTempleEvents(std::vector<DungeonEvent>& out);
void RegisterZulFarrakEvents(std::vector<DungeonEvent>& out);
void RegisterShadowfangKeepEvents(std::vector<DungeonEvent>& out);
void RegisterScarletMonasteryEvents(std::vector<DungeonEvent>& out);
void RegisterRazorfenDownsEvents(std::vector<DungeonEvent>& out);
void RegisterBlackrockDepthsEvents(std::vector<DungeonEvent>& out);
void RegisterDeadminesEvents(std::vector<DungeonEvent>& out);
void RegisterWailingCavernsEvents(std::vector<DungeonEvent>& out);
void RegisterStratholmeEvents(std::vector<DungeonEvent>& out);
void RegisterUldamanEvents(std::vector<DungeonEvent>& out);
void RegisterScholomanceEvents(std::vector<DungeonEvent>& out);
void RegisterDireMaulEvents(std::vector<DungeonEvent>& out);
// Hellfire Ramparts (map 543) final-approach gate — see HellfireRampartsEvents.cpp
// for the measurements. Exposed so t/TestRampartsLedgeProbe can assert against the
// real navmesh that the zone-in platform lies outside the gate; the numbers are
// only meaningful together with that probe.
namespace DcHellfireRamparts
{
    // How far to scan for the Hellfire Sentries / Vazruden.
    constexpr float FINAL_APPROACH_SCAN = 45.0f;
    // Floor Z the bot must be above: the upper level the final platform sits on.
    constexpr float FINAL_APPROACH_MIN_Z = 76.0f;
}

void RegisterHellfireRampartsEvents(std::vector<DungeonEvent>& out);
void RegisterBloodFurnaceEvents(std::vector<DungeonEvent>& out);
void RegisterSlavePensEvents(std::vector<DungeonEvent>& out);
void RegisterUnderbogEvents(std::vector<DungeonEvent>& out);
void RegisterOldHillsbradEvents(std::vector<DungeonEvent>& out);
void RegisterMechanarEvents(std::vector<DungeonEvent>& out);
void RegisterShatteredHallsEvents(std::vector<DungeonEvent>& out);
void RegisterSteamvaultEvents(std::vector<DungeonEvent>& out);
void RegisterArcatrazEvents(std::vector<DungeonEvent>& out);
void RegisterSethekkHallsEvents(std::vector<DungeonEvent>& out);
void RegisterBlackMorassEvents(std::vector<DungeonEvent>& out);
// Everything in the Black Morass wave that DRAINS Medivh's shield rather than
// fighting the party: the nine trash adds (smart_scripts SMART_EVENT_RESET ->
// CAST 'Corrupt Medivh' 31326 on SELF) AND AEONUS, whose boss_aeonus::
// IsSummonedBy does the same thing in C++ and whose 37853 drains at DOUBLE the
// rate. All of them spawn REACT_DEFENSIVE and park at a home 14yd from Medivh, so
// they never aggro the party and the engage pipeline's natural pull never reaches
// them — the wave driver (ObjectiveHookRegistry hook 12) force-pulls them and
// counts them to decide when Medivh's ring needs cleaning. Excludes the Rift
// Lords / Keepers and the wave-6/12 bosses, which all fight normally.
std::vector<uint32> const& BlackMorassDrainEntries();

// The Black Morass RIFT KEEPERS — the mob each Time Rift summons 6s after it
// opens, and the ONLY thing whose death closes the rift
// (npc_time_rift::SummonedCreatureDies -> DespawnOrUnsummon). Shared with the
// wave driver (ObjectiveHookRegistry hook 12), which selects and pulls by it.
// Disjoint from BlackMorassDrainEntries() on purpose: keepers fight normally and
// never channel Corrupt, so they are never sweep targets — and the drainers never
// close a rift, so they are never selection targets.
//
// AEONUS IS NOT HERE despite being the wave-18 boss: it walks off to Medivh the
// instant it spawns (so it is never at the rift to select on) and it is not its
// rift's _riftKeeperGUID (so killing it closes nothing). It is a drainer.
std::vector<uint32> const& BlackMorassKeeperEntries();

// Wrath of the Lich King.
void RegisterUtgardeKeepEvents(std::vector<DungeonEvent>& out);
void RegisterNexusEvents(std::vector<DungeonEvent>& out);
void RegisterAzjolNerubEvents(std::vector<DungeonEvent>& out);
void RegisterAhnkahetEvents(std::vector<DungeonEvent>& out);
// Drak'Tharon Keep (map 600) — Novos' camp, shared with the hold driver
// (ObjectiveHookRegistry hook 14, HoldNovosCamp) so the camp and the keep-out it
// is placed against have exactly ONE definition. Every number is measured; the
// reasoning is in DrakTharonKeepEvents.cpp.
namespace DcDrakTharonKeep
{
    constexpr uint32 NOVOS = 26631;

    // Column-probed against the live 600 mmtiles: one walkable surface at
    // z 28.39. 19.3yd from Novos (-379.27, -737.73), 14.9yd from the Fetid Troll
    // Corpses' arrival point, 56yd from the staircase spawn trigger.
    constexpr float CAMP_X = -379.0f;
    constexpr float CAMP_Y = -757.0f;
    constexpr float CAMP_Z = 28.4f;

    // Grid-scan radius for Novos, from the activation predicate and the driver
    // alike. The chamber is ~96 x 88yd; this must cover it and the approach
    // without reaching Trollgore's arena 206yd away.
    constexpr float NOVOS_SCAN = 120.0f;

    // Keep-out around Novos while the Arcane Field (47346) is up. THIS MUST TRACK
    // the map-600 47346 row's placement `radius` in DcHazardRegistry — the driver
    // and the placement solver have to agree on one cylinder, and
    // t/TestDcHazard's DrakTharonArcaneFieldKeepOutAgreesWithTheNovosCamp pins
    // the camp against it.
    constexpr float FIELD_KEEPOUT = 14.0f;

    // Re-centring leash (the tank comes home past this UNLESS it is in melee
    // contact) and the hard leash (it comes home regardless). The hard leash is
    // sized to catch the three places phase 1 can strand a party — the staircase
    // at 56yd, ROOM_LEFT at 40yd and ROOM_RIGHT at 50yd — while leaving the
    // corpse arrival point at 14.9yd comfortably inside.
    constexpr float CAMP_LEASH = 6.0f;
    constexpr float CAMP_HARD_LEASH = 25.0f;
}

void RegisterDrakTharonKeepEvents(std::vector<DungeonEvent>& out);

// The Violet Hold (map 608) — the numbers the declarative half
// (VioletHoldEvents.cpp) and the imperative half (VioletHoldDriver.cpp) must
// agree on. Every one of them is either read straight out of violet_hold.h or
// probed against the live 608 mmtile; the reasoning is in VioletHoldEvents.cpp.
// Shared here for the same reason DcDrakTharonKeep is: a camp and the keep-out
// it is placed against need exactly ONE definition, and the gtests pin them
// against each other.
namespace DcVioletHold
{
    constexpr uint32 MAP = 608;

    // Creature entries (violet_hold.h VHCreatures).
    constexpr uint32 NPC_SINCLARI             = 30658;
    constexpr uint32 NPC_PRISON_DOOR_SEAL     = 30896;
    constexpr uint32 NPC_TELEPORTATION_PORTAL = 31011;
    constexpr uint32 NPC_CYANIGOSA            = 31134;
    constexpr uint32 NPC_ICHORON              = 29313;
    constexpr uint32 NPC_ICHOR_GLOBULE        = 29321;

    // The aura every wave add parks on the Prison Door Seal
    // (violet_hold_trashAI::CreatureStartAttackDoor -> DoCastAOE). Spell.dbc:
    // effect 6, aura 23 SPELL_AURA_PERIODIC_TRIGGER_SPELL, amplitude 3000ms,
    // DurationIndex 21 = INFINITE. spell_destroy_door_seal_aura turns each tick
    // into one ACTION_DECREASE_DOOR_HEALTH, so ONE add at the door costs the
    // 100-point gate one point every three seconds: 300s to lose the run alone,
    // 150s for two, 100s for three. That arithmetic is what sizes SEAL_DIRTY_MIN.
    constexpr uint32 SPELL_DESTROY_DOOR_SEAL  = 58040;

    // GetData ids (violet_hold.h VHData). _gateHealth has NO GetData case, which
    // is why the driver reads the seal's aura instead of the drain level.
    constexpr uint32 DATA_ENCOUNTER_STATUS    = 30;
    constexpr uint32 DATA_WAVE_COUNT          = 33;

    // GetBossState slots (violet_hold.h VHBosses). NOT DungeonEncounter bits:
    // the released pair is rolled per instance, so killing Zuramat as the first
    // prisoner sets no bit that names Zuramat. Completion rides these.
    constexpr uint32 BOSS_STATE_1ST           = 0;
    constexpr uint32 BOSS_STATE_2ND           = 1;
    constexpr uint32 BOSS_STATE_CYANIGOSA     = 2;

    // ObjectiveHookRegistry ids. Three defend hooks, not one: a Custom step is
    // handed a default-constructed DungeonBossInfo, so a shared hook cannot tell
    // which objective invoked it.
    constexpr uint32 HOOK_START               = 15;
    constexpr uint32 HOOK_DEFEND_1ST          = 16;
    constexpr uint32 HOOK_DEFEND_2ND          = 17;
    constexpr uint32 HOOK_DEFEND_CYANIGOSA    = 18;
    constexpr uint32 HOOK_DRIVE_WAVE          = 19;

    // THE DOOR CAMP — the EMERGENCY position, not the default one.
    //
    // On the flat door landing at the top of the entrance ramp (the ramp runs
    // z 38.6 at x~1869.8 up to z 44.0 at x~1861.5), straddling the funnel that
    // the last two waypoints of ALL SIX trash paths run through — (1858.95,
    // 810.05), (1860.84, 806.65), (1861.54, 804.15) and (1857.81, 796.77) all
    // lie within 7yd of it. 10.4yd inside the convergence midpoint and 32.4yd
    // from the Prison Seal itself, so the party is between the adds and the door
    // without standing on the door. Column-probed against the live 608 mmtile:
    // exactly ONE walkable surface, z 44.23.
    //
    // The first cut of this dungeon made this the party's STANDING position and
    // let the siege walk to it. That reading of the chokepoint is arithmetically
    // tidy and is not how the Violet Hold is played, or won: a keeper portal
    // pumps 3-4 adds every 20 seconds FOREVER and the only off-switch is 52-86yd
    // away at the rim, so a party that waits at the door fights the pump's output
    // instead of the pump and falls further behind every cycle. The party now
    // stations at the live portal (STAGE / rule 5 in VhDriveWave) and comes BACK
    // here only when something has actually reached the seal.
    constexpr float CAMP_X = 1855.0f;
    constexpr float CAMP_Y = 803.5f;
    constexpr float CAMP_Z = 44.05f;

    // THE STAGING POINT — where the party waits when no portal is open.
    //
    // The middle of the arena floor, at the foot of the entrance ramp: the core's
    // own MiddleRoomLocation, which is where Cyanigosa MoveJumps to on wave 18 and
    // 4.4yd from the wave-6/12 saboteur portal, so it is a position the encounter
    // itself treats as the centre of the fight. Column-probed against the live 608
    // mmtile: exactly ONE walkable surface, z 38.89.
    //
    // It exists because the party's job between waves is to be CLOSE TO THE NEXT
    // PORTAL, and the six rim portals average 45.5yd from here against 69.0yd from
    // the door camp (worst case 59.0 against 85.5). That is ~3.5s off every hop, on
    // a clock where a keeper portal starts pumping 30s after it opens. Every trash
    // path also runs through this half of the room on its way to the door, so
    // waiting here still meets the leftovers of the previous wave head-on.
    constexpr float STAGE_X = 1892.29f;
    constexpr float STAGE_Y = 805.70f;
    constexpr float STAGE_Z = 38.44f;

    // Where every wave add ends up: the midpoint of the two convergence points
    // (1843.71, 805.81, 44.14) — paths 0, 1, 1-alt, 2, 5 — and (1845.58, 800.68,
    // 44.10) — paths 3, 4, and also violet_hold_trashAI::EnterEvadeMode's new
    // home. The driver measures "how dirty is the seal" from here when the aura
    // read is unavailable.
    constexpr float SEAL_X = 1844.6f;
    constexpr float SEAL_Y = 803.2f;
    constexpr float SEAL_Z = 44.12f;

    // Arena centroid (mean of the six rim portal positions), used only by the
    // wave event's proximity gate, and the grid-scan radius that covers the whole
    // hold. The room is ~110yd across and the far rim portal is 86yd from the
    // camp; 200 sees all of it from anywhere the driver roams.
    constexpr float ARENA_X = 1893.1f;
    constexpr float ARENA_Y = 804.7f;
    constexpr float ARENA_SCAN = 200.0f;
    constexpr float EVENT_DUE_RANGE = 200.0f;

    // "Released" for a caged prisoner (and for Erekem's two guards): the
    // instance's StartBossEncounter clears UNIT_FLAG_NON_ATTACKABLE and
    // SetImmuneToNPC/All(false) at the moment the cell opens, so the flags ARE
    // the release latch. Also false through Ichoron's shattered-bubble window,
    // when he carries UNIT_FLAG_NOT_SELECTABLE for 15s — which is what makes the
    // driver retarget to his Ichor Globules instead of standing in drop-target
    // limbo. Defined in VioletHoldEvents.cpp.
    bool IsReleased(Creature const* c);
}

void RegisterVioletHoldEvents(std::vector<DungeonEvent>& out);

// Every TempSummon the siege can field — the trash, the elites, the three portal
// keepers, Ichoron's globules, Xevozz's spheres and Cyanigosa. Probed by
// ALIVENESS by the wave event's activation predicate, which is sound only
// because none of them exists before the encounter creates it. The six caged
// prisoners and Erekem's guards are world spawns and are NOT here — see
// VioletHoldPrisonerEntries().
std::vector<uint32> const& VioletHoldWaveEntries();

// Portal Guardian 30660 / Portal Keeper 30695 / 30893 — the ONLY thing whose
// death closes a keeper portal and stops its 20-second, never-ending add pump
// (npc_vh_teleportation_portal kills itself once nothing is left to channel
// 58012 on). Shared with the wave driver, which selects and travels by it.
// Deliberately disjoint from the trash: keepers never walk to the door and never
// drain the seal, and no trash mob ever closes a portal.
std::vector<uint32> const& VioletHoldKeeperEntries();

// The six caged prisoners plus Erekem's two guards. World spawns, present from
// map load behind sealed cells, so they are probed with DcVioletHold::IsReleased
// (the instance clears NON_ATTACKABLE / IMMUNE_TO_PC on release) and never for
// mere aliveness — an aliveness probe on these would read true on an inert
// dungeon and hand the wave driver the tick before the party had even entered.
std::vector<uint32> const& VioletHoldPrisonerEntries();

// --- roster patches (one appender per dungeon that corrects the boss list) -
// Each relocates that dungeon's BossRosterPatch out of BossRosterRegistry.cpp
// so a dungeon's whole clear definition lives in one file. Aggregated by
// PatchTable() (BossRosterRegistry.cpp). Only dungeons that patch the derived
// roster appear here (e.g. Shadowfang Keep / Blood Furnace have events but no
// patch, so no roster appender).
void RegisterScarletMonasteryRoster(std::vector<BossRosterPatch>& t);
void RegisterScholomanceRoster(std::vector<BossRosterPatch>& t);
void RegisterSunkenTempleRoster(std::vector<BossRosterPatch>& t);
void RegisterRazorfenDownsRoster(std::vector<BossRosterPatch>& t);
void RegisterZulFarrakRoster(std::vector<BossRosterPatch>& t);
void RegisterBlackrockDepthsRoster(std::vector<BossRosterPatch>& t);
void RegisterDeadminesRoster(std::vector<BossRosterPatch>& t);
void RegisterWailingCavernsRoster(std::vector<BossRosterPatch>& t);
void RegisterStratholmeRoster(std::vector<BossRosterPatch>& t);
void RegisterDireMaulRoster(std::vector<BossRosterPatch>& t);
void RegisterUldamanRoster(std::vector<BossRosterPatch>& t);
void RegisterHellfireRampartsRoster(std::vector<BossRosterPatch>& t);
void RegisterSlavePensRoster(std::vector<BossRosterPatch>& t);
void RegisterUnderbogRoster(std::vector<BossRosterPatch>& t);
void RegisterOldHillsbradRoster(std::vector<BossRosterPatch>& t);
void RegisterMechanarRoster(std::vector<BossRosterPatch>& t);
void RegisterShatteredHallsRoster(std::vector<BossRosterPatch>& t);
void RegisterSteamvaultRoster(std::vector<BossRosterPatch>& t);
void RegisterArcatrazRoster(std::vector<BossRosterPatch>& t);
void RegisterSethekkHallsRoster(std::vector<BossRosterPatch>& t);
void RegisterBlackMorassRoster(std::vector<BossRosterPatch>& t);
void RegisterMaraudonRoster(std::vector<BossRosterPatch>& t);
void RegisterUtgardeKeepRoster(std::vector<BossRosterPatch>& t);
void RegisterNexusRoster(std::vector<BossRosterPatch>& t);
void RegisterAzjolNerubRoster(std::vector<BossRosterPatch>& t);
void RegisterAhnkahetRoster(std::vector<BossRosterPatch>& t);
void RegisterDrakTharonKeepRoster(std::vector<BossRosterPatch>& t);
void RegisterVioletHoldRoster(std::vector<BossRosterPatch>& t);

// --- wing layouts (one appender per split map) ---------------------------
// Records which boss credit-entries belong to which wing of a multi-wing map;
// aggregated by DungeonWingRegistry. Only split maps appear here. Maraudon has
// no events (wings + one roster removal) and lives in MaraudonEvents.cpp.
void RegisterDireMaulWings(std::unordered_map<uint32, DungeonWingLayout>& store);
void RegisterScarletMonasteryWings(std::unordered_map<uint32, DungeonWingLayout>& store);
void RegisterMaraudonWings(std::unordered_map<uint32, DungeonWingLayout>& store);

// --- anchor routes (one appender per dungeon that hand-authors a route) ---
// Waypoint anchors StridedPathfinder walks INSTEAD of asking the navmesh
// pathfinder for a corridor, for stretches where the mesh defeats it. These
// take no `out` parameter — they call DungeonClearRouteRegistry::Register
// directly — and are invoked from DungeonClearRouteRegistry's own one-time
// seed, for the same linkage reason as the tables above.
void RegisterAzjolNerubRoute();

#endif
