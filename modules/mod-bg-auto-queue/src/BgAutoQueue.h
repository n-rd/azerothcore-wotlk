/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license
 */

#ifndef _MOD_BG_AUTO_QUEUE_H_
#define _MOD_BG_AUTO_QUEUE_H_

#include "DBCEnums.h"
#include "ObjectGuid.h"
#include "Optional.h"
#include "SharedDefines.h"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

class Battleground;
class Player;
struct PvPDifficultyEntry;

// PlayerSettings layout for this module. source = "mod-bg-auto-queue".
enum BgAutoQueueSetting
{
    BG_AUTO_QUEUE_SETTING_OPT_OUT = 0,      // 0 = opted in (default), 1 = manual opt-out, 2 = auto opt-out
    BG_AUTO_QUEUE_SETTING_DECLINE_COUNT = 1 // consecutive module invites declined/ignored
};

class BgAutoQueue
{
public:
    static BgAutoQueue* instance();

    void LoadConfig();

    bool IsEnabled() const { return _enabled; }

    // Opt-out is stored as a per-character core PlayerSetting; these are thin
    // wrappers over Player::GetPlayerSetting/UpdatePlayerSetting.
    bool IsOptedOut(Player* player) const;
    bool IsAutoOptedOut(Player* player) const;
    void SetOptOut(Player* player, bool optedOut);

    // Decline tracking, driven by the module PlayerScript: every
    // CMSG_BATTLEFIELD_PORT answer, the invite-expired desertion hook, and
    // logout cleanup. Only queue entries this module created are affected.
    void HandleBattleFieldPort(Player* player, uint8 arenaType, BattlegroundTypeId bgTypeId, uint8 action);
    void HandleInviteExpired(Player* player);
    void UntrackPlayer(ObjectGuid guid);

    bool IsLevelEligible(uint8 level) const;

    // Why an online player was not queued during a pass. Reported by
    // .bgevents run so an operator can tell exactly which filter fired
    // (e.g. an opted-out character looks identical to a wrong-level one
    // from the outside).
    enum class SkipReason : uint8
    {
        NotInWorld = 0,
        Bot,                // playerbot session (SkipBots)
        OptedOut,
        Level,
        Dungeon,
        InBattleground,
        AlreadyQueued,
        Deserter,
        Lfg,
        DeathKnightEbonHold,
        GameMaster,
        Aura,               // carries one of the configured skip auras
        Afk,                // flagged AFK
        NoBracket,          // no PvP bracket for the level on the reference map
        Count
    };

    static char const* GetSkipReasonLabel(SkipReason reason);

    // Full gather-time per-player queue eligibility: every IsEligible gate plus
    // the reference-map bracket check (NoBracket). On false, *reason is the first
    // failing gate; on true, *bracket (when non-null) gets the resolved bracket.
    // Public so .bgevents can report real eligibility with the pass's own logic.
    bool IsQueueEligible(Player* player, SkipReason* reason = nullptr,
        PvPDifficultyEntry const** bracket = nullptr) const;

    // Outcome of a queue pass, reported back to .bgevents run so an operator
    // can see whether anyone was actually queued, broken down per bracket,
    // plus why the rest were skipped.
    struct QueuePassResult
    {
        struct BracketCount
        {
            uint32 minLevel = 0;
            uint32 maxLevel = 0;
            uint32 players = 0;
        };

        uint32 players = 0;                 // total players queued across all brackets
        std::vector<BracketCount> brackets; // one entry per bracket that queued >= 1 player, sorted by level

        uint32 considered = 0;              // online players examined this pass
        std::array<uint32, static_cast<size_t>(SkipReason::Count)> skipped{}; // per-reason skip tally
        uint32 bracketsWithoutBg = 0;       // populated brackets with no eligible BG to queue into
        uint32 skippedAtQueueTime = 0;      // eligible players dropped by re-check/veto at queue time
    };

    // Runs a single per-bracket queue pass. Does NOT check _enabled — it is
    // invoked both by the periodic Update (enabled path) and by .bgevents run
    // (always), so the Enable/Interval gate lives only in Update.
    QueuePassResult RunQueuePass();

    // Drives the periodic queue pass and the post-pass backlog draining.
    // Call from WorldScript::OnUpdate.
    void Update(uint32 diff);

    // Milliseconds until the next automatic pass (0 when no pass is scheduled).
    uint32 GetTimeUntilNextPass() const;

private:
    BgAutoQueue() = default;

    // Per-bracket bucket of eligible players gathered during a pass. Players
    // are stored by GUID and re-resolved at queue time (never store a
    // long-lived Player*). Level and faction are cached at gather time for the
    // per-bracket partition math; the whole pass runs synchronously inside one
    // world tick, so they cannot change before queue time.
    struct BucketMember
    {
        ObjectGuid guid;
        uint8 level = 0;
        TeamId team = TEAM_NEUTRAL;
    };

    struct BracketBucket
    {
        std::vector<BucketMember> players;
        uint32 minLevel = 0; // bracket level range, drives waiter counting and logging
        uint32 maxLevel = 0;
    };

    // Shared per-player eligibility used by IsQueueEligible and by the
    // queue-time re-check in QueueBucket. Excludes the BG-specific
    // OnPlayerCanJoinInBattlegroundQueue veto (that runs only at queue time).
    // When non-null, reason is set to the first failing filter (only
    // meaningful when this returns false).
    bool IsEligible(Player* player, SkipReason* reason = nullptr) const;

    // True when the player can be queued into bgTypeId at their level.
    bool CanEnter(Player* player, BattlegroundTypeId bgTypeId) const;

    // True when at least one bucket player can be queued into bgTypeId at their
    // level (subset-friendly; QueueBucket skips the rest at queue time).
    bool BucketHasAnyFit(BattlegroundTypeId bgTypeId, BracketBucket const& bucket) const;

    // True when at least one bucket player resolves, on this live game's own map,
    // to this game's own bracket and has BG access for its type.
    bool BucketFitsLiveBg(Battleground* bg, BracketBucket const& bucket) const;

    struct QueuedWaiters
    {
        uint32 total = 0;
        uint32 alliance = 0;
        uint32 horde = 0;
    };

    // Counts uninvited players already sitting in bgTypeId's core queue for one
    // map-relative bracket, split by the players' real faction (RealTeamID,
    // immune to CFBG's staging mutations of teamId). Only the queue buckets the
    // currently-active matcher reads are counted (BG_QUEUE_CFBG while the
    // mirrored CFBG.Enable is on, the premade/faction buckets otherwise):
    // entries stranded on the wrong side of a CFBG.Enable reload can never pop,
    // so counting them would inflate viability and steer passes into dead
    // queues; they are surfaced by a once-per-load warning instead. Groups
    // already invited to a forming instance are skipped.
    QueuedWaiters CountUninvitedWaitersInBracket(BattlegroundTypeId bgTypeId,
        BattlegroundBracketId bracketId) const;

    // Sums CountUninvitedWaitersInBracket over the candidate BG's own
    // map-relative bracket(s) spanning [minLevel, maxLevel] (bracket indices
    // are numbered per map; brackets are contiguous, so the two endpoints
    // cover the whole range).
    QueuedWaiters CountUninvitedWaiters(BattlegroundTypeId bgTypeId,
        uint32 minLevel, uint32 maxLevel) const;

    // One candidate-map bracket's share of a bucket. Core matching pops
    // strictly per bracket_id, so viability must be judged on these
    // partitions, never on the pooled bucket total (AV's shifted brackets
    // split the 60-69 and 70-79 reference buckets in two; a pooled total can
    // look viable while every partition is below minimum).
    struct BracketPartition
    {
        PvPDifficultyEntry const* bracket = nullptr;
        uint32 total = 0;
        uint32 alliance = 0;
        uint32 horde = 0;
    };

    // Partitions the bucket's players by their own bracket on the candidate
    // BG's map, applying the same per-player gates QueueBucket applies (the
    // battleground_template level window and bracket existence), so partition
    // counts match what would actually be queued.
    std::vector<BracketPartition> PartitionByCandidateBracket(Battleground* bgTemplate,
        BracketBucket const& bucket) const;

    // Bracket-aware MinPlayersPerTeam used only to rank fallback candidates
    // when nothing is viable: resolves the bucket's bracket on the candidate
    // BG's own map and returns the same override-aware minimum core matching
    // uses (GetMinPlayersPerTeam, BattlegroundUtils.h). Bucket levels come
    // from the WSG reference map, so the candidate may have no bracket at
    // minLevel (hence the maxLevel retry) or none at all, in which case the
    // raw template minimum is returned.
    uint32 GetEffectiveMinPlayersPerTeam(Battleground* bgTemplate, BracketBucket const& bucket) const;

    // Viability per CrossFaction: cross-faction => total >= 2*min; otherwise
    // each faction tally >= min. Judged independently for every non-empty
    // bracket partition (see BracketPartition) with that partition's own
    // waiters and override-aware minimum; all partitions must pass. Includes
    // uninvited players already queued for the candidate BG, not just the
    // freshly-gathered batch.
    bool IsViable(Battleground* bgTemplate, BracketBucket const& bucket) const;

    // Selects the BG for a populated bracket: live-BG reinforcement first
    // (not limited to the pool), then the pool candidate that already has
    // uninvited queuers, then a random pick from the configured pool with
    // documented fallbacks. On a live-BG pick, liveBracket carries the matched
    // game's own map-relative bracket id; it stays empty on every other path.
    BattlegroundTypeId SelectBattlegroundForBracket(BracketBucket const& bucket,
        Optional<BattlegroundBracketId>& liveBracket) const;

    // Queues every player in the bucket into bgTypeId, then schedules one
    // queue update per distinct bracket queued into and registers the bracket
    // for backlog draining (see UpdatePendingDrains). When liveBracket is set
    // (live-BG reinforcement), players whose own bracket differs are skipped —
    // that game's queue list would never serve them. The whole verified bucket
    // is queued, including an odd solo: under strict CFBG EvenTeams the matcher
    // invites the even subset and leaves the odd one queued as backfill for the
    // next decline. Returns the number of players queued. When non-null,
    // skippedAtQueueTime is incremented for each bucket player dropped by the
    // queue-time re-check, the BG-specific veto, or the level/bracket mismatch.
    uint32 QueueBucket(BattlegroundTypeId bgTypeId, BracketBucket const& bucket,
        Optional<BattlegroundBracketId> liveBracket, uint32* skippedAtQueueTime = nullptr);

    // Any normal-BG template works for the deserter/permission check
    // (Player::CanJoinToBattleground is template-type-independent for normal
    // battlegrounds); individual templates can be absent (disabled via
    // `disables` or removed), so the first existing one is used. Warns once
    // per config load when none exists so the gate never silently vanishes.
    Battleground* GetDeserterCheckTemplate() const;

    // One (queue, battleground, bracket) the module queued players into that
    // may still hold more than one instance's worth of uninvited waiters.
    // Core's non-rated queue update creates at most ONE new instance per call
    // and nothing re-triggers it for normal BG queues on its own (the periodic
    // scheduler only serves arena queues; an all-accept invite wave leaves no
    // re-trigger), so a mass batch would drain one instance per pass without
    // this.
    struct PendingDrain
    {
        BattlegroundQueueTypeId queueTypeId = BATTLEGROUND_QUEUE_NONE;
        BattlegroundTypeId bgTypeId = BATTLEGROUND_TYPE_NONE;
        BattlegroundBracketId bracketId = BG_BRACKET_ID_FIRST;
    };

    void RegisterPendingDrain(BattlegroundQueueTypeId queueTypeId,
        BattlegroundTypeId bgTypeId, BattlegroundBracketId bracketId);

    // Re-issues one ScheduleQueueUpdate per tracked entry every few seconds
    // while the entry's bracket still holds enough uninvited waiters to pop
    // another instance; entries below that threshold are dropped (declines and
    // manual joins re-trigger the core queue on their own). Runs even while
    // the periodic pass is disabled so a `.bgevents run` backlog still drains.
    void UpdatePendingDrains(uint32 diff);

    void BroadcastWarning() const;

    // Shared decline path (explicit decline and expired invite): counter,
    // auto-opt-out, and the player-facing messages.
    void OnModuleInviteDeclined(Player* player);

    // remaining = declines left before the auto-opt-out; 0 omits the
    // countdown line (OptOutAfterDeclines disabled).
    void SendDeclineHint(Player* player, uint32 remaining) const;
    void SendOptOutNotice(Player* player, uint32 count) const;

    // One queue entry the module created for a player. joinTime is the core
    // GroupQueueInfo::JoinTime captured at AddGroup: queue removals can happen
    // without any hook firing (invited BG deleted/ended before the invite ran
    // out), so consumers must match joinTime to be sure an entry is still ours
    // and not a later manual re-queue of the same queue type.
    struct TrackedQueue
    {
        BattlegroundQueueTypeId queueTypeId = BATTLEGROUND_QUEUE_NONE;
        uint32 joinTime = 0;
    };

    // Rebuilds _pool from the configured _poolRaw against the battleground
    // templates that exist right now. Called at the start of every pass so a
    // pass always reflects the current templates (templates are not yet loaded
    // at config-load time, and may change on .reload). Cheap: _poolRaw is tiny.
    void ResolvePool();

    bool _enabled = true;
    uint32 _levelMin = 10;
    uint32 _levelMax = 79;
    std::vector<BattlegroundTypeId> _poolRaw; // type ids parsed from config, unvalidated
    std::vector<BattlegroundTypeId> _pool;    // _poolRaw filtered to usable templates
    uint32 _intervalMs = 45u * 60u * 1000u;
    uint32 _initialDelayMs = 0;
    uint32 _warningLeadMs = 60u * 1000u;
    bool _crossFaction = true;
    bool _cfbgEnabled = false;     // mirrored CFBG.Enable; selects which queue buckets the matcher reads
    bool _skipBots = true;
    bool _skipGameMasters = true;
    bool _skipAfk = true;
    std::vector<uint32> _skipAuras; // aura ids that exclude a player from a pass
    std::string _broadcastMessage;
    bool _declineHintEnabled = true;
    uint32 _optOutAfterDeclines = 3;

    uint32 _elapsedMs = 0;
    bool _warningSent = false;
    bool _firstPass = true;
    // Latches the CrossFaction/matcher WARN to once per config load (mutable:
    // CountUninvitedWaiters is const).
    mutable bool _matcherWarnLogged = false;
    // Latches the stale-queue-bucket WARN (entries unreachable after a
    // CFBG.Enable reload) to once per config load.
    mutable bool _staleQueueWarnLogged = false;
    // Latches the no-deserter-template WARN to once per config load.
    mutable bool _deserterWarnLogged = false;
    // First ResolvePool after a config load reports rejected entries at WARN
    // (visible with stock log levels); later passes repeat them at DEBUG.
    bool _poolResolveLogged = false;

    std::vector<PendingDrain> _pendingDrains;
    uint32 _drainTimerMs = 0;

    std::unordered_map<ObjectGuid, TrackedQueue> _trackedQueues;
};

#define sBgAutoQueue BgAutoQueue::instance()

#endif // _MOD_BG_AUTO_QUEUE_H_
