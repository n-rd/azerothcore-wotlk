/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license
 */

#include "BgAutoQueue.h"

#include "AreaDefines.h"
#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "BattlegroundQueue.h"
#include "BattlegroundUtils.h"
#include "Chat.h"
#include "Config.h"
#include "Containers.h"
#include "DBCStores.h"
#include "DisableMgr.h"
#include "GameTime.h"
#include "LFGMgr.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include "Tokenize.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <utility>

namespace
{
    // Used as reference (Warsong Gulch spans for every eligible level)
    constexpr uint32 BG_BRACKET_REFERENCE_MAP = MAP_WARSONG_GULCH; // 489

    // Normal (non-arena, non-random) battleground types
    constexpr BattlegroundTypeId BG_NORMAL_TYPES[] =
    {
        BATTLEGROUND_AV,
        BATTLEGROUND_WS,
        BATTLEGROUND_AB,
        BATTLEGROUND_EY,
        BATTLEGROUND_SA,
        BATTLEGROUND_IC
    };

    // How often UpdatePendingDrains re-checks queued-into brackets for a
    // backlog worth another instance. Cheap (a few queue-list scans), and one
    // extra instance can pop per check, so a mass batch drains within
    // seconds instead of one instance per 45-minute pass.
    constexpr uint32 BG_AUTO_QUEUE_DRAIN_CHECK_MS = 5000;

    // default for BgAutoQueue.BroadcastMessage config
    constexpr char const* BG_AUTO_QUEUE_DEFAULT_BROADCAST =
        "A Battleground event is starting shortly. Type .bgevents off to opt "
        "out, or .bgevents on to opt back in. You can also join manually by "
        "pressing H.";
}

BgAutoQueue* BgAutoQueue::instance()
{
    static BgAutoQueue instance;
    return &instance;
}

void BgAutoQueue::LoadConfig()
{
    _enabled  = sConfigMgr->GetOption<bool>("BgAutoQueue.Enable", true);
    _levelMin = sConfigMgr->GetOption<uint32>("BgAutoQueue.Level.Min", 10);
    _levelMax = sConfigMgr->GetOption<uint32>("BgAutoQueue.Level.Max", 79);

    if (_levelMin > _levelMax)
    {
        LOG_WARN("module", "BgAutoQueue level range is inverted ({} > {}), swapping.", _levelMin, _levelMax);
        std::swap(_levelMin, _levelMax);
    }

    // Only parse the type ids here; battleground templates do not exist yet at
    // config-load time, so template/arena validation is left to ResolvePool.
    _poolRaw.clear();
    std::string const poolStr = sConfigMgr->GetOption<std::string>("BgAutoQueue.Pool", "2,3,7");
    for (std::string_view token : Acore::Tokenize(poolStr, ',', false))
    {
        Optional<uint32> value = Acore::StringTo<uint32>(Acore::String::Trim(std::string(token)));
        if (!value)
        {
            LOG_WARN("module", "BgAutoQueue.Pool entry '{}' is not a valid number, ignoring.", token);
            continue;
        }

        BattlegroundTypeId bgTypeId = static_cast<BattlegroundTypeId>(*value);
        if (bgTypeId == BATTLEGROUND_RB)
        {
            LOG_WARN("module", "BgAutoQueue.Pool entry {} is Random Battleground (unsupported), ignoring.", *value);
            continue;
        }

        _poolRaw.push_back(bgTypeId);
    }

    if (_poolRaw.empty())
        LOG_WARN("module", "BgAutoQueue.Pool is empty; the random pick and waiter reinforcement are disabled (only live-battleground reinforcement can queue players).");

    // Timers are stored in uint32 milliseconds; clamp instead of silently
    // wrapping (a wrapped Interval can land in the seconds range and turn the
    // module into a continuous mass-queue loop).
    constexpr uint64 maxTimerMs = std::numeric_limits<uint32>::max();

    uint32 const intervalMin = sConfigMgr->GetOption<uint32>("BgAutoQueue.Interval", 45);
    uint64 const intervalMs = uint64(intervalMin) * 60 * 1000;
    if (intervalMs > maxTimerMs)
        LOG_WARN("module", "BgAutoQueue.Interval ({} min) exceeds the supported maximum, clamping to {} min.", intervalMin, maxTimerMs / (60 * 1000));
    _intervalMs = static_cast<uint32>(std::min(intervalMs, maxTimerMs));

    uint32 const initialDelaySec = sConfigMgr->GetOption<uint32>("BgAutoQueue.InitialDelay", 0);
    uint64 const initialDelayMs = uint64(initialDelaySec) * 1000;
    if (initialDelayMs > maxTimerMs)
        LOG_WARN("module", "BgAutoQueue.InitialDelay ({} s) exceeds the supported maximum, clamping to {} s.", initialDelaySec, maxTimerMs / 1000);
    _initialDelayMs = static_cast<uint32>(std::min(initialDelayMs, maxTimerMs));

    uint32 const warningLeadSec = sConfigMgr->GetOption<uint32>("BgAutoQueue.WarningLeadTime", 60);
    uint64 const warningLeadMs = uint64(warningLeadSec) * 1000;
    if (warningLeadMs > maxTimerMs)
        LOG_WARN("module", "BgAutoQueue.WarningLeadTime ({} s) exceeds the supported maximum, clamping to {} s.", warningLeadSec, maxTimerMs / 1000);
    _warningLeadMs = static_cast<uint32>(std::min(warningLeadMs, maxTimerMs));

    if (_intervalMs > 0 && _warningLeadMs >= _intervalMs)
        LOG_WARN("module", "BgAutoQueue.WarningLeadTime ({} s) >= Interval ({} min); the warning will not fire.", warningLeadSec, intervalMin);

    if (_initialDelayMs > 0 && _warningLeadMs >= _initialDelayMs)
        LOG_WARN("module", "BgAutoQueue.WarningLeadTime ({} s) >= InitialDelay ({} s); no warning will fire before the first pass.", warningLeadSec, initialDelaySec);

    _crossFaction     = sConfigMgr->GetOption<bool>("BgAutoQueue.CrossFaction", true);

    // Mirror mod-cfbg's CFBG.Enable (showLogs=false because absent CFBG.* keys
    // are the normal state on installs without mod-cfbg). While it is on,
    // mod-cfbg routes every non-rated queue entry through the cross-faction
    // bucket -- the bucket the matcher's waiter counting reads.
    _cfbgEnabled = sConfigMgr->GetOption<bool>("CFBG.Enable", false, false);

    if (_cfbgEnabled && !_crossFaction)
        LOG_WARN("module", "mod-bg-auto-queue: CFBG.Enable = 1 but BgAutoQueue.CrossFaction = 0. "
            "mod-cfbg fills both teams from the total player count, so per-faction viability "
            "under-queues; set BgAutoQueue.CrossFaction = 1 while mod-cfbg is active.");

    _skipGameMasters  = sConfigMgr->GetOption<bool>("BgAutoQueue.SkipGameMasters", true);
    _skipAfk          = sConfigMgr->GetOption<bool>("BgAutoQueue.SkipAFK", true);
    _broadcastMessage = sConfigMgr->GetOption<std::string>("BgAutoQueue.BroadcastMessage", BG_AUTO_QUEUE_DEFAULT_BROADCAST);

    _declineHintEnabled  = sConfigMgr->GetOption<bool>("BgAutoQueue.DeclineHint.Enable", true);
    _optOutAfterDeclines = sConfigMgr->GetOption<uint32>("BgAutoQueue.OptOutAfterDeclines", 3);

    _skipAuras.clear();
    std::string const skipAurasStr = sConfigMgr->GetOption<std::string>("BgAutoQueue.SkipAuras", "");
    for (std::string_view token : Acore::Tokenize(skipAurasStr, ',', false))
    {
        Optional<uint32> value = Acore::StringTo<uint32>(Acore::String::Trim(std::string(token)));
        if (!value)
        {
            LOG_WARN("module", "BgAutoQueue.SkipAuras entry '{}' is not a valid number, ignoring.", token);
            continue;
        }

        _skipAuras.push_back(*value);
    }

    // Reset timing on (re)load. Reload re-applies InitialDelay — accepted.
    // _pendingDrains survives a reload on purpose: it only nudges the core
    // queue to serve players a previous pass already queued, and every entry
    // is re-validated against the current templates before each nudge.
    // _trackedQueues survives too: entries self-validate via JoinTime, and
    // clearing them would silence the decline hint for anyone queued by a
    // pass that ran just before the reload.
    _elapsedMs            = 0;
    _warningSent          = false;
    _firstPass            = true;
    _matcherWarnLogged    = false;
    _staleQueueWarnLogged = false;
    _deserterWarnLogged   = false;
    _poolResolveLogged    = false;

    LOG_INFO("module", "mod-bg-auto-queue: enabled={}, levels=[{}-{}], configured pool size={}, interval={} min, initialDelay={} s, warningLead={} s, crossFaction={}, cfbg={}, skipGM={}, skipAFK={}, skipAuras={}, declineHint={}, optOutAfterDeclines={}.",
        _enabled, _levelMin, _levelMax, _poolRaw.size(), intervalMin, initialDelaySec, warningLeadSec, _crossFaction, _cfbgEnabled, _skipGameMasters, _skipAfk, _skipAuras.size(), _declineHintEnabled, _optOutAfterDeclines);

    // Opt-out is stored via the core PlayerSettings system, which only persists
    // across logins when EnablePlayerSettings is on. Without it, .bgevents
    // opt-out still works but only for the current session.
    if (!sWorld->getBoolConfig(CONFIG_PLAYER_SETTINGS_ENABLED))
        LOG_WARN("module", "mod-bg-auto-queue: EnablePlayerSettings is 0; "
            ".bgevents opt-out works only for the current session and will not "
            "persist across logins until an administrator sets EnablePlayerSettings = 1.");
}

void BgAutoQueue::ResolvePool()
{
    // First resolve after a config load reports at WARN so the operator sees
    // rejects with stock log levels; ResolvePool runs every pass, so later
    // repeats drop to DEBUG.
    bool const firstResolve = !_poolResolveLogged;
    auto poolLog = [firstResolve](std::string const& message)
    {
        if (firstResolve)
            LOG_WARN("module", "{}", message);
        else
            LOG_DEBUG("module", "{}", message);
    };

    _pool.clear();
    for (BattlegroundTypeId bgTypeId : _poolRaw)
    {
        Battleground* bgTemplate = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId);
        if (!bgTemplate)
        {
            // A battleground disabled in the `disables` table at startup gets
            // no template at all, so this path covers both bad IDs and disables.
            poolLog(Acore::StringFormat("BgAutoQueue.Pool entry {} has no battleground template (unknown ID, or disabled in the `disables` table), ignoring.", static_cast<uint32>(bgTypeId)));
            continue;
        }

        if (bgTemplate->isArena())
        {
            poolLog(Acore::StringFormat("BgAutoQueue.Pool entry {} is an arena (unsupported), ignoring.", static_cast<uint32>(bgTypeId)));
            continue;
        }

        // Runtime disables (`.reload disables` after startup) keep the
        // template, so the no-template path above does not catch them.
        if (sDisableMgr->IsDisabledFor(DISABLE_TYPE_BATTLEGROUND, bgTypeId, nullptr))
        {
            poolLog(Acore::StringFormat("BgAutoQueue.Pool entry {} is disabled in the `disables` table, ignoring.", static_cast<uint32>(bgTypeId)));
            continue;
        }

        _pool.push_back(bgTypeId);
    }

    if (_pool.empty() && !_poolRaw.empty())
        poolLog("BgAutoQueue.Pool has no usable battlegrounds against the current templates; the random pick and waiter reinforcement are disabled (only live-battleground reinforcement can queue players).");

    if (firstResolve)
        LOG_INFO("module", "mod-bg-auto-queue: pool resolved: {} usable of {} configured entries.", _pool.size(), _poolRaw.size());

    _poolResolveLogged = true;
}

bool BgAutoQueue::IsOptedOut(Player* player) const
{
    // GetPlayerSetting is non-const (it lazily creates a zero-default entry),
    // but the constness here is on BgAutoQueue, not on the Player* argument.
    // Not IsEnabled(): that is an equality-with-1 check and would miss the
    // auto-opt-out value 2.
    return player->GetPlayerSetting("mod-bg-auto-queue", BG_AUTO_QUEUE_SETTING_OPT_OUT).value != 0;
}

bool BgAutoQueue::IsAutoOptedOut(Player* player) const
{
    return player->GetPlayerSetting("mod-bg-auto-queue", BG_AUTO_QUEUE_SETTING_OPT_OUT).value == 2;
}

void BgAutoQueue::SetOptOut(Player* player, bool optedOut)
{
    player->UpdatePlayerSetting("mod-bg-auto-queue", BG_AUTO_QUEUE_SETTING_OPT_OUT, optedOut ? 1u : 0u);
    // The counter describes consecutive declines within the current opt-in
    // period only, so any explicit toggle restarts it.
    player->UpdatePlayerSetting("mod-bg-auto-queue", BG_AUTO_QUEUE_SETTING_DECLINE_COUNT, 0);
}

bool BgAutoQueue::IsLevelEligible(uint8 level) const
{
    return level >= _levelMin && level <= _levelMax;
}

char const* BgAutoQueue::GetSkipReasonLabel(SkipReason reason)
{
    switch (reason)
    {
        case SkipReason::NotInWorld:          return "not in world";
        case SkipReason::OptedOut:            return "opted out (.bgevents off)";
        case SkipReason::Level:               return "level outside configured range";
        case SkipReason::Dungeon:             return "in a dungeon/raid";
        case SkipReason::InBattleground:      return "already in a battleground";
        case SkipReason::AlreadyQueued:       return "already queued / no free queue slot";
        case SkipReason::Deserter:            return "deserter or cannot join";
        case SkipReason::Lfg:                 return "using the LFG system";
        case SkipReason::DeathKnightEbonHold: return "Death Knight locked to Ebon Hold";
        case SkipReason::GameMaster:          return "game master";
        case SkipReason::Aura:                return "has a configured skip aura";
        case SkipReason::Afk:                 return "flagged AFK";
        case SkipReason::NoBracket:           return "no PvP bracket for level";
        default:                              return "unknown";
    }
}

bool BgAutoQueue::IsEligible(Player* player, SkipReason* reason) const
{
    auto fail = [reason](SkipReason value)
    {
        if (reason)
            *reason = value;
        return false;
    };

    if (!player || !player->IsInWorld())
        return fail(SkipReason::NotInWorld);

    std::string const& name = player->GetName();

    if (IsOptedOut(player))
    {
        LOG_DEBUG("module", "mod-bg-auto-queue: skip {}: opted out.", name);
        return fail(SkipReason::OptedOut);
    }

    if (!IsLevelEligible(player->GetLevel()))
    {
        LOG_DEBUG("module", "mod-bg-auto-queue: skip {}: level {} outside [{}-{}].", name, player->GetLevel(), _levelMin, _levelMax);
        return fail(SkipReason::Level);
    }

    if (player->GetMap()->IsDungeon())
    {
        LOG_DEBUG("module", "mod-bg-auto-queue: skip {}: in a dungeon/raid.", name);
        return fail(SkipReason::Dungeon);
    }

    if (player->InBattleground())
    {
        LOG_DEBUG("module", "mod-bg-auto-queue: skip {}: already in a battleground.", name);
        return fail(SkipReason::InBattleground);
    }

    if (player->InBattlegroundQueue() || !player->HasFreeBattlegroundQueueId())
    {
        LOG_DEBUG("module", "mod-bg-auto-queue: skip {}: already queued or no free queue slot.", name);
        return fail(SkipReason::AlreadyQueued);
    }

    // Deserter/permission check via any normal-BG template.
    if (Battleground* bgTemplate = GetDeserterCheckTemplate())
    {
        if (!player->CanJoinToBattleground(bgTemplate))
        {
            LOG_DEBUG("module", "mod-bg-auto-queue: skip {}: CanJoinToBattleground=false (deserter?).", name);
            return fail(SkipReason::Deserter);
        }
    }

    // LFG: mirror the core BG join handler rule.
    lfg::LfgState lfgState = sLFGMgr->GetState(player->GetGUID());
    if (lfgState > lfg::LFG_STATE_NONE
        && (lfgState != lfg::LFG_STATE_QUEUED || !sWorld->getBoolConfig(CONFIG_ALLOW_JOIN_BG_AND_LFG)))
    {
        LOG_DEBUG("module", "mod-bg-auto-queue: skip {}: using the LFG system.", name);
        return fail(SkipReason::Lfg);
    }

    // Death Knights still locked to Ebon Hold cannot be teleported to a BG yet.
    if (player->IsClass(CLASS_DEATH_KNIGHT, CLASS_CONTEXT_TELEPORT)
        && player->GetMapId() == MAP_EBON_HOLD
        && !player->IsGameMaster()
        && !player->HasSpell(50977))
    {
        LOG_DEBUG("module", "mod-bg-auto-queue: skip {}: Death Knight not yet allowed to leave Ebon Hold.", name);
        return fail(SkipReason::DeathKnightEbonHold);
    }

    if (_skipGameMasters && player->IsGameMaster())
    {
        LOG_DEBUG("module", "mod-bg-auto-queue: skip {}: game master.", name);
        return fail(SkipReason::GameMaster);
    }

    if (_skipAfk && player->isAFK())
    {
        LOG_DEBUG("module", "mod-bg-auto-queue: skip {}: flagged AFK.", name);
        return fail(SkipReason::Afk);
    }

    for (uint32 auraId : _skipAuras)
    {
        if (player->HasAura(auraId))
        {
            LOG_DEBUG("module", "mod-bg-auto-queue: skip {}: has skip aura {}.", name, auraId);
            return fail(SkipReason::Aura);
        }
    }

    return true;
}

bool BgAutoQueue::IsQueueEligible(Player* player, SkipReason* reason, PvPDifficultyEntry const** bracket) const
{
    if (!IsEligible(player, reason))
        return false;

    PvPDifficultyEntry const* bracketEntry = GetBattlegroundBracketByLevel(BG_BRACKET_REFERENCE_MAP, player->GetLevel());
    if (!bracketEntry)
    {
        if (reason)
            *reason = SkipReason::NoBracket;
        return false;
    }

    if (bracket)
        *bracket = bracketEntry;
    return true;
}

Battleground* BgAutoQueue::GetDeserterCheckTemplate() const
{
    // CanJoinToBattleground only reads the template's arena/RB type, which is
    // identical for every normal battleground, so any existing template works.
    for (BattlegroundTypeId bgTypeId : BG_NORMAL_TYPES)
        if (Battleground* bgTemplate = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId))
            return bgTemplate;

    if (!_deserterWarnLogged)
    {
        LOG_WARN("module", "mod-bg-auto-queue: no normal battleground template exists; the deserter/permission eligibility check is skipped.");
        _deserterWarnLogged = true;
    }

    return nullptr;
}

bool BgAutoQueue::CanEnter(Player* player, BattlegroundTypeId bgTypeId) const
{
    if (!player)
        return false;

    Battleground* bgTemplate = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId);
    if (!bgTemplate)
        return false;

    if (!player->GetBGAccessByLevel(bgTypeId))
        return false;

    // A BG without a PvP bracket entry for the player's level cannot be queued.
    return GetBattlegroundBracketByLevel(bgTemplate->GetMapId(), player->GetLevel()) != nullptr;
}

bool BgAutoQueue::BucketHasAnyFit(BattlegroundTypeId bgTypeId, BracketBucket const& bucket) const
{
    for (BucketMember const& member : bucket.players)
    {
        Player* player = ObjectAccessor::FindPlayer(member.guid);
        if (player && CanEnter(player, bgTypeId))
            return true;
    }

    return false;
}

bool BgAutoQueue::BucketFitsLiveBg(Battleground* bg, BracketBucket const& bucket) const
{
    BattlegroundTypeId const bgTypeId = bg->GetBgTypeID();
    for (BucketMember const& member : bucket.players)
    {
        Player* player = ObjectAccessor::FindPlayer(member.guid);
        if (!player || !player->GetBGAccessByLevel(bgTypeId))
            continue;

        PvPDifficultyEntry const* entry = GetBattlegroundBracketByLevel(bg->GetMapId(), player->GetLevel());
        if (entry && entry->GetBracketId() == bg->GetBracketId())
            return true;
    }

    return false;
}

BgAutoQueue::QueuedWaiters BgAutoQueue::CountUninvitedWaitersInBracket(BattlegroundTypeId bgTypeId,
    BattlegroundBracketId bracketId) const
{
    QueuedWaiters waiters;

    BattlegroundQueueTypeId bgQueueTypeId = BattlegroundMgr::BGQueueTypeId(bgTypeId, 0);
    if (bgQueueTypeId == BATTLEGROUND_QUEUE_NONE)
        return waiters;

    BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);

    static constexpr BattlegroundQueueGroupTypes WAITER_GROUP_TYPES[] =
    {
        BG_QUEUE_PREMADE_ALLIANCE,
        BG_QUEUE_PREMADE_HORDE,
        BG_QUEUE_NORMAL_ALLIANCE,
        BG_QUEUE_NORMAL_HORDE,
        BG_QUEUE_CFBG
    };

    for (BattlegroundQueueGroupTypes groupType : WAITER_GROUP_TYPES)
    {
        // mod-cfbg serves only BG_QUEUE_CFBG while enabled; stock matching
        // serves only the premade/faction buckets. Entries on the inactive
        // side (queued before a CFBG.Enable flip + reload) can never pop, so
        // they must not count as waiters — warn instead, with the diagnosis
        // matching the direction of the flip.
        bool const matcherReads = _cfbgEnabled == (groupType == BG_QUEUE_CFBG);

        for (GroupQueueInfo const* gInfo : bgQueue.m_QueuedGroups[bracketId][groupType])
        {
            if (gInfo->IsInvitedToBGInstanceGUID != 0)
                continue;

            if (!matcherReads)
            {
                if (!_staleQueueWarnLogged)
                {
                    if (_cfbgEnabled)
                        LOG_WARN("module", "mod-bg-auto-queue: uninvited players sit in premade/faction "
                            "queue buckets that the cross-faction matcher (CFBG.Enable = 1) never reads. "
                            "They were queued before mod-cfbg was enabled and can never be invited; they "
                            "are excluded from viability counts and must leave and re-join the queue "
                            "(or log out).");
                    else
                        LOG_WARN("module", "mod-bg-auto-queue: uninvited players sit in the cross-faction "
                            "queue bucket while CFBG.Enable is 0/absent. They were queued before mod-cfbg "
                            "was disabled and can never be invited; they are excluded from viability "
                            "counts and must leave and re-join the queue (or log out).");
                    _staleQueueWarnLogged = true;
                }
                // Nothing in a non-read bucket is counted, and one uninvited
                // entry suffices for the diagnostic, so skip the rest.
                break;
            }

            if (_crossFaction && !_cfbgEnabled && !_matcherWarnLogged
                && (groupType == BG_QUEUE_NORMAL_ALLIANCE || groupType == BG_QUEUE_NORMAL_HORDE))
            {
                LOG_WARN("module", "mod-bg-auto-queue: BgAutoQueue.CrossFaction = 1 but uninvited "
                    "players sit in a faction-specific queue bucket, so no cross-faction matcher "
                    "(e.g. mod-cfbg) appears to be active. Total-based viability can queue "
                    "faction-lopsided batches the core queue never pops. Set "
                    "BgAutoQueue.CrossFaction = 0 on a stock core.");
                _matcherWarnLogged = true;
            }

            uint32 const count = static_cast<uint32>(gInfo->Players.size());
            waiters.total += count;
            if (gInfo->RealTeamID == TEAM_ALLIANCE)
                waiters.alliance += count;
            else
                waiters.horde += count;
        }
    }

    return waiters;
}

BgAutoQueue::QueuedWaiters BgAutoQueue::CountUninvitedWaiters(BattlegroundTypeId bgTypeId,
    uint32 minLevel, uint32 maxLevel) const
{
    QueuedWaiters waiters;

    Battleground* bgTemplate = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId);
    if (!bgTemplate)
        return waiters;

    // Bracket indices are numbered per map, so the level range must be resolved
    // against the candidate BG's own map. Brackets are contiguous, so the two
    // endpoints cover the whole range (0, 1, or 2 distinct ids); a level with no
    // bracket on this map cannot enter this BG and contributes nothing.
    std::vector<BattlegroundBracketId> bracketIds;
    for (uint32 level : { minLevel, maxLevel })
        if (PvPDifficultyEntry const* entry = GetBattlegroundBracketByLevel(bgTemplate->GetMapId(), level))
            if (std::find(bracketIds.begin(), bracketIds.end(), entry->GetBracketId()) == bracketIds.end())
                bracketIds.push_back(entry->GetBracketId());

    for (BattlegroundBracketId bracketId : bracketIds)
    {
        QueuedWaiters const bracketWaiters = CountUninvitedWaitersInBracket(bgTypeId, bracketId);
        waiters.total += bracketWaiters.total;
        waiters.alliance += bracketWaiters.alliance;
        waiters.horde += bracketWaiters.horde;
    }

    return waiters;
}

std::vector<BgAutoQueue::BracketPartition> BgAutoQueue::PartitionByCandidateBracket(Battleground* bgTemplate,
    BracketBucket const& bucket) const
{
    std::vector<BracketPartition> partitions;

    for (BucketMember const& member : bucket.players)
    {
        // Mirror the queue-time per-player gates: the battleground_template
        // level window (a template, so GetMin/MaxLevel are the DB values) and
        // the DBC bracket lookup.
        if (member.level < bgTemplate->GetMinLevel() || member.level > bgTemplate->GetMaxLevel())
            continue;

        PvPDifficultyEntry const* entry = GetBattlegroundBracketByLevel(bgTemplate->GetMapId(), member.level);
        if (!entry)
            continue;

        auto itr = std::find_if(partitions.begin(), partitions.end(),
            [entry](BracketPartition const& partition)
            {
                return partition.bracket == entry;
            });
        if (itr == partitions.end())
        {
            partitions.push_back({ entry, 0, 0, 0 });
            itr = std::prev(partitions.end());
        }

        ++itr->total;
        if (member.team == TEAM_ALLIANCE)
            ++itr->alliance;
        else
            ++itr->horde;
    }

    return partitions;
}

uint32 BgAutoQueue::GetEffectiveMinPlayersPerTeam(Battleground* bgTemplate, BracketBucket const& bucket) const
{
    PvPDifficultyEntry const* bracketEntry = GetBattlegroundBracketByLevel(bgTemplate->GetMapId(), bucket.minLevel);
    if (!bracketEntry)
        bracketEntry = GetBattlegroundBracketByLevel(bgTemplate->GetMapId(), bucket.maxLevel);

    if (!bracketEntry)
        return bgTemplate->GetMinPlayersPerTeam();

    return ::GetMinPlayersPerTeam(bgTemplate, bracketEntry);
}

bool BgAutoQueue::IsViable(Battleground* bgTemplate, BracketBucket const& bucket) const
{
    // Core matching pops strictly per bracket_id, so every non-empty partition
    // must reach the minimum on its own — a pooled bucket total can look
    // viable while each split bracket stays below minimum forever (AV's
    // shifted brackets straddle the reference buckets).
    std::vector<BracketPartition> const partitions = PartitionByCandidateBracket(bgTemplate, bucket);
    if (partitions.empty())
        return false;

    for (BracketPartition const& partition : partitions)
    {
        uint32 const minPerTeam = ::GetMinPlayersPerTeam(bgTemplate, partition.bracket);
        QueuedWaiters const waiters = CountUninvitedWaitersInBracket(bgTemplate->GetBgTypeID(), partition.bracket->GetBracketId());

        if (_crossFaction)
        {
            if (partition.total + waiters.total < 2u * minPerTeam)
                return false;
        }
        else if (partition.alliance + waiters.alliance < minPerTeam
            || partition.horde + waiters.horde < minPerTeam)
            return false;
    }

    return true;
}

BattlegroundTypeId BgAutoQueue::SelectBattlegroundForBracket(BracketBucket const& bucket,
    Optional<BattlegroundBracketId>& liveBracket) const
{
    // (a) Live-BG reinforcement (priority; not limited to the pool).
    std::vector<std::pair<BattlegroundTypeId, BattlegroundBracketId>> liveTypes;
    for (BattlegroundTypeId bgTypeId : BG_NORMAL_TYPES)
    {
        if (sDisableMgr->IsDisabledFor(DISABLE_TYPE_BATTLEGROUND, bgTypeId, nullptr))
            continue;

        for (Battleground* bg : sBattlegroundMgr->GetBGFreeSlotQueueStore(bgTypeId))
        {
            if (!(bg->GetStatus() > STATUS_WAIT_QUEUE && bg->GetStatus() < STATUS_WAIT_LEAVE))
                continue;

            if (!bg->HasFreeSlots())
                continue;

            // Match each live game by its own map-relative bracket, reinforced by
            // whichever subset of bucket players fits it (off-boundary players are
            // skipped at queue time, not blocking the whole game).
            if (!BucketFitsLiveBg(bg, bucket))
                continue;

            liveTypes.push_back({ bgTypeId, bg->GetBracketId() });
            break;
        }
    }

    if (!liveTypes.empty())
    {
        auto const& [bgTypeId, bracketId] = Acore::Containers::SelectRandomContainerElement(liveTypes);
        liveBracket = bracketId;
        return bgTypeId;
    }

    // (a2) Prefer the pool candidate that already has uninvited queuers, so a
    // manual queuer's chosen BG is reinforced instead of bypassed. Among viable
    // candidates pick the one closest to popping (most waiters); ties at
    // random. Restricted to the configured pool: otherwise a single manual
    // queuer could steer every cycle's mass-queue into a battleground the
    // operator deliberately excluded.
    std::vector<BattlegroundTypeId> waiterLeaders;
    uint32 bestWaiters = 0;
    for (BattlegroundTypeId bgTypeId : _pool)
    {
        if (sDisableMgr->IsDisabledFor(DISABLE_TYPE_BATTLEGROUND, bgTypeId, nullptr))
            continue;

        if (!BucketHasAnyFit(bgTypeId, bucket))
            continue;

        QueuedWaiters const waiters = CountUninvitedWaiters(bgTypeId, bucket.minLevel, bucket.maxLevel);
        if (waiters.total == 0)
            continue;

        Battleground* bgTemplate = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId);
        if (!bgTemplate || !IsViable(bgTemplate, bucket))
            continue;

        if (waiters.total > bestWaiters)
        {
            bestWaiters = waiters.total;
            waiterLeaders.clear();
            waiterLeaders.push_back(bgTypeId);
        }
        else if (waiters.total == bestWaiters)
            waiterLeaders.push_back(bgTypeId);
    }

    if (!waiterLeaders.empty())
        return Acore::Containers::SelectRandomContainerElement(waiterLeaders);

    // (b) Random pick from the configured pool.
    std::vector<BattlegroundTypeId> candidates;
    for (BattlegroundTypeId bgTypeId : _pool)
    {
        if (sDisableMgr->IsDisabledFor(DISABLE_TYPE_BATTLEGROUND, bgTypeId, nullptr))
            continue;

        if (BucketHasAnyFit(bgTypeId, bucket))
            candidates.push_back(bgTypeId);
    }

    if (candidates.empty())
        return BATTLEGROUND_TYPE_NONE;

    if (candidates.size() == 1)
        return candidates.front();

    std::vector<BattlegroundTypeId> viable;
    for (BattlegroundTypeId bgTypeId : candidates)
    {
        Battleground* bgTemplate = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId);
        if (bgTemplate && IsViable(bgTemplate, bucket))
            viable.push_back(bgTypeId);
    }

    if (!viable.empty())
        return Acore::Containers::SelectRandomContainerElement(viable);

    // None viable: pick the smallest by MinPlayersPerTeam (ties -> lowest id).
    BattlegroundTypeId best = BATTLEGROUND_TYPE_NONE;
    uint32 bestMin = std::numeric_limits<uint32>::max();
    for (BattlegroundTypeId bgTypeId : candidates)
    {
        Battleground* bgTemplate = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId);
        if (!bgTemplate)
            continue;

        uint32 const minPerTeam = GetEffectiveMinPlayersPerTeam(bgTemplate, bucket);
        if (minPerTeam < bestMin || (minPerTeam == bestMin && bgTypeId < best))
        {
            bestMin = minPerTeam;
            best = bgTypeId;
        }
    }

    return best;
}

uint32 BgAutoQueue::QueueBucket(BattlegroundTypeId bgTypeId, BracketBucket const& bucket,
    Optional<BattlegroundBracketId> liveBracket, uint32* skippedAtQueueTime)
{
    Battleground* bgTemplate = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId);
    if (!bgTemplate)
        return 0;

    BattlegroundQueueTypeId bgQueueTypeId = BattlegroundMgr::BGQueueTypeId(bgTypeId, 0);
    if (bgQueueTypeId == BATTLEGROUND_QUEUE_NONE)
        return 0;

    BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);

    // Phase 1: re-validate and collect. Holding Player* between the two phases
    // is safe: both run inside this one synchronous call, no tick boundary.
    std::vector<std::pair<Player*, PvPDifficultyEntry const*>> verified;

    for (BucketMember const& member : bucket.players)
    {
        Player* player = ObjectAccessor::FindPlayer(member.guid);
        if (!player)
            continue;

        // Re-validate: state may have changed since the bucket was gathered.
        if (!IsEligible(player) || player->InBattlegroundQueueForBattlegroundQueueType(bgQueueTypeId))
        {
            if (skippedAtQueueTime)
                ++*skippedAtQueueTime;
            continue;
        }

        // BG-specific veto (can only run at queue time).
        GroupJoinBattlegroundResult err = ERR_BATTLEGROUND_NONE;
        if (!sScriptMgr->OnPlayerCanJoinInBattlegroundQueue(player, ObjectGuid::Empty, bgTypeId, 0, err))
        {
            LOG_DEBUG("module", "mod-bg-auto-queue: skip {}: OnPlayerCanJoinInBattlegroundQueue veto.", player->GetName());
            if (skippedAtQueueTime)
                ++*skippedAtQueueTime;
            continue;
        }

        // The core join handler refuses players outside the
        // battleground_template level window — a free-form DB range distinct
        // from the DBC bracket below, so operators can restrict a BG to a
        // narrower range than its brackets.
        if (!player->GetBGAccessByLevel(bgTypeId))
        {
            if (skippedAtQueueTime)
                ++*skippedAtQueueTime;
            continue;
        }

        PvPDifficultyEntry const* bracketEntry = GetBattlegroundBracketByLevel(bgTemplate->GetMapId(), player->GetLevel());
        if (!bracketEntry)
        {
            if (skippedAtQueueTime)
                ++*skippedAtQueueTime;
            continue;
        }

        // Tier-a reinforcement targets one live game; a player whose own bracket
        // differs would sit in a queue list that game never serves.
        if (liveBracket && bracketEntry->GetBracketId() != *liveBracket)
        {
            LOG_DEBUG("module", "mod-bg-auto-queue: skip {}: level {} is outside the live game's bracket.", player->GetName(), player->GetLevel());
            if (skippedAtQueueTime)
                ++*skippedAtQueueTime;
            continue;
        }

        verified.push_back({ player, bracketEntry });
    }

    // Queue the whole verified bucket, including an odd solo: under strict CFBG
    // EvenTeams the matcher invites the even subset and leaves the odd one
    // queued, where it becomes exactly the backfill the queue needs -- the next
    // queue update after any decline pulls it in via FillPlayersToCFBG, and a
    // manual 6th queuer arriving mid-interval can pop a match with it.

    // Phase 2: queue everyone that survived.
    uint32 queued = 0;
    std::vector<BattlegroundBracketId> scheduledBrackets;

    for (auto const& [player, bracketEntry] : verified)
    {
        GroupQueueInfo* ginfo = bgQueue.AddGroup(player, nullptr, bgTypeId, bracketEntry, 0, false, false, 0, 0);
        uint32 avgWaitTime = bgQueue.GetAverageQueueWaitTime(ginfo);
        uint32 queueSlot = player->AddBattlegroundQueueId(bgQueueTypeId);

        // Overwriting a stale entry is fine: gather-time eligibility rejects
        // anyone already in a BG queue, so the player holds no other live
        // module entry.
        _trackedQueues[player->GetGUID()] = { bgQueueTypeId, ginfo->JoinTime };

        if (WorldSession* session = player->GetSession())
        {
            WorldPacket data;
            sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bgTemplate, queueSlot, STATUS_WAIT_QUEUE, avgWaitTime, 0, 0, TEAM_NEUTRAL);
            session->SendPacket(&data);
        }

        sScriptMgr->OnPlayerJoinBG(player);

        if (std::find(scheduledBrackets.begin(), scheduledBrackets.end(), bracketEntry->GetBracketId()) == scheduledBrackets.end())
            scheduledBrackets.push_back(bracketEntry->GetBracketId());
        ++queued;

        LOG_DEBUG("module", "mod-bg-auto-queue: queued {} into bgTypeId {}.", player->GetName(), static_cast<uint32>(bgTypeId));
    }

    // One queue update per distinct bracket queued into, not once per player.
    // The bracket is also registered for draining: core creates at most one
    // new instance per queue update, so a batch larger than one instance
    // needs follow-up updates (see UpdatePendingDrains).
    for (BattlegroundBracketId scheduledBracket : scheduledBrackets)
    {
        sBattlegroundMgr->ScheduleQueueUpdate(0, 0, bgQueueTypeId, bgTypeId, scheduledBracket);
        RegisterPendingDrain(bgQueueTypeId, bgTypeId, scheduledBracket);
        LOG_DEBUG("module", "mod-bg-auto-queue: scheduled queue update for bgTypeId {} bracket {}.", static_cast<uint32>(bgTypeId), static_cast<uint32>(scheduledBracket));
    }

    return queued;
}

void BgAutoQueue::UntrackPlayer(ObjectGuid guid)
{
    _trackedQueues.erase(guid);
}

void BgAutoQueue::HandleBattleFieldPort(Player* player, uint8 arenaType, BattlegroundTypeId bgTypeId, uint8 action)
{
    auto itr = _trackedQueues.find(player->GetGUID());
    if (itr == _trackedQueues.end())
        return;

    TrackedQueue const tracked = itr->second;
    BattlegroundQueueTypeId const queueTypeId = BattlegroundMgr::BGQueueTypeId(bgTypeId, arenaType);

    if (action == 1) // accept ("Enter Battle")
    {
        // A successful accept removes the player from ALL other BG queues, so
        // the tracking entry is dead whichever queue was accepted. A rare
        // failed accept (e.g. freeze aura) untracks early — benign direction:
        // a hint is missed, a false hint can never fire.
        _trackedQueues.erase(itr);

        GroupQueueInfo ginfo;
        if (queueTypeId == tracked.queueTypeId
            && sBattlegroundMgr->GetBattlegroundQueue(queueTypeId).GetPlayerGroupInfoData(player->GetGUID(), &ginfo)
            && ginfo.JoinTime == tracked.joinTime)
            player->UpdatePlayerSetting("mod-bg-auto-queue", BG_AUTO_QUEUE_SETTING_DECLINE_COUNT, 0);

        return;
    }

    // action == 0: leave queue / decline. A different queue means a manual
    // queue action; the module entry is untouched.
    if (queueTypeId != tracked.queueTypeId)
        return;

    GroupQueueInfo ginfo;
    if (!sBattlegroundMgr->GetBattlegroundQueue(queueTypeId).GetPlayerGroupInfoData(player->GetGUID(), &ginfo)
        || ginfo.JoinTime != tracked.joinTime)
    {
        // Stale: our entry left the queue without any hook (invited BG was
        // deleted or ended) and this answers a later manual re-queue.
        _trackedQueues.erase(itr);
        return;
    }

    _trackedQueues.erase(itr);

    // No live invite = pre-invite leave via the queue icon (the documented
    // group-play workflow): no message, no count.
    if (ginfo.IsInvitedToBGInstanceGUID != 0)
        OnModuleInviteDeclined(player);
}

void BgAutoQueue::HandleInviteExpired(Player* player)
{
    auto itr = _trackedQueues.find(player->GetGUID());
    if (itr == _trackedQueues.end())
        return;

    TrackedQueue const tracked = itr->second;

    GroupQueueInfo ginfo;
    if (!sBattlegroundMgr->GetBattlegroundQueue(tracked.queueTypeId).GetPlayerGroupInfoData(player->GetGUID(), &ginfo)
        || ginfo.JoinTime != tracked.joinTime)
    {
        _trackedQueues.erase(itr);
        return;
    }

    // The expiry hook carries no queue identity, but RemoveInviteTime pins it
    // down: the invite that just ran out is due (<= now), while a concurrent
    // invite from another (manual) queue still lies in the future. When it
    // isn't ours, keep tracking — our entry's own expiry or the player's
    // answer lands here later. Same-batch double expiry counts once: the
    // first call untracks, the second finds nothing.
    if (ginfo.IsInvitedToBGInstanceGUID == 0
        || GameTime::GetGameTimeMS().count() < ginfo.RemoveInviteTime)
        return;

    _trackedQueues.erase(itr);
    OnModuleInviteDeclined(player);
}

void BgAutoQueue::OnModuleInviteDeclined(Player* player)
{
    // Opted out while queued (opting out never dequeues): don't nag, don't count.
    if (IsOptedOut(player))
        return;

    uint32 count = 0;
    if (_optOutAfterDeclines > 0)
    {
        count = player->GetPlayerSetting("mod-bg-auto-queue", BG_AUTO_QUEUE_SETTING_DECLINE_COUNT).value + 1;
        player->UpdatePlayerSetting("mod-bg-auto-queue", BG_AUTO_QUEUE_SETTING_DECLINE_COUNT, count);

        if (count >= _optOutAfterDeclines)
        {
            // Value 2 marks the opt-out as automatic (distinct .bgevents
            // status text). Not SetOptOut: that writes 1 and resets the
            // counter. The notice ignores _declineHintEnabled — a silent
            // auto-opt-out is never acceptable.
            player->UpdatePlayerSetting("mod-bg-auto-queue", BG_AUTO_QUEUE_SETTING_OPT_OUT, 2);
            SendOptOutNotice(player, count);
            LOG_DEBUG("module", "mod-bg-auto-queue: {} auto-opted out after {} consecutive declined events.",
                player->GetName(), count);
            return;
        }
    }

    if (_declineHintEnabled)
        SendDeclineHint(player, _optOutAfterDeclines > 0 ? _optOutAfterDeclines - count : 0);
}

void BgAutoQueue::SendDeclineHint(Player* player, uint32 remaining) const
{
    WorldSession* session = player->GetSession();
    if (!session)
        return;

    std::string text =
        "|cffff8800|||r\n"
        "|cffff8800|||r If you no longer wish to be automatically invited to Battlegrounds, type: |cff00ccff.bgevents off|r\n";

    if (remaining == 1)
        text += "|cffff8800|||r If you decline one more event, automatic invites will stop on their own.\n";
    else if (remaining > 1)
        text += Acore::StringFormat("|cffff8800|||r If you decline {} more events in a row, automatic invites will stop on their own.\n", remaining);

    text += "|cffff8800|||r";

    ChatHandler handler(session);
    handler.SendSysMessage(text);
    handler.SendNotification("You can turn off automatic BG invites (see chat).");
}

void BgAutoQueue::SendOptOutNotice(Player* player, uint32 count) const
{
    WorldSession* session = player->GetSession();
    if (!session)
        return;

    std::string const text = Acore::StringFormat(
        "|cffff8800|||r\n"
        "|cffff8800|||r You have declined {} Battleground events in a row, so automatic invites are now turned off for this character.\n"
        "|cffff8800|||r Type |cff00ccff.bgevents on|r if you want them back.\n"
        "|cffff8800|||r",
        count);

    ChatHandler handler(session);
    handler.SendSysMessage(text);
    handler.SendNotification("Automatic BG invites are now off (see chat).");
}

void BgAutoQueue::RegisterPendingDrain(BattlegroundQueueTypeId queueTypeId,
    BattlegroundTypeId bgTypeId, BattlegroundBracketId bracketId)
{
    for (PendingDrain const& drain : _pendingDrains)
        if (drain.queueTypeId == queueTypeId && drain.bgTypeId == bgTypeId && drain.bracketId == bracketId)
            return;

    _pendingDrains.push_back({ queueTypeId, bgTypeId, bracketId });
}

void BgAutoQueue::UpdatePendingDrains(uint32 diff)
{
    if (_pendingDrains.empty())
        return;

    _drainTimerMs += diff;
    if (_drainTimerMs < BG_AUTO_QUEUE_DRAIN_CHECK_MS)
        return;

    _drainTimerMs = 0;

    for (auto itr = _pendingDrains.begin(); itr != _pendingDrains.end();)
    {
        // The core's scheduled queue-update path has no disable check, so
        // nudging a runtime-disabled battleground could still pop instances.
        // Queueing players into it after a re-enable re-registers the drain.
        if (sDisableMgr->IsDisabledFor(DISABLE_TYPE_BATTLEGROUND, itr->bgTypeId, nullptr))
        {
            itr = _pendingDrains.erase(itr);
            continue;
        }

        Battleground* bgTemplate = sBattlegroundMgr->GetBattlegroundTemplate(itr->bgTypeId);
        PvPDifficultyEntry const* bracketEntry = bgTemplate
            ? GetBattlegroundBracketById(bgTemplate->GetMapId(), itr->bracketId)
            : nullptr;

        uint32 const minPerTeam = bracketEntry ? ::GetMinPlayersPerTeam(bgTemplate, bracketEntry) : 0;
        if (!minPerTeam)
        {
            itr = _pendingDrains.erase(itr);
            continue;
        }

        // Drop the entry once the bracket can no longer fill another instance;
        // below that, invite declines and manual joins re-trigger the core
        // queue on their own.
        QueuedWaiters const waiters = CountUninvitedWaitersInBracket(itr->bgTypeId, itr->bracketId);
        bool const canPop = _crossFaction
            ? waiters.total >= 2u * minPerTeam
            : (waiters.alliance >= minPerTeam && waiters.horde >= minPerTeam);

        if (!canPop)
        {
            itr = _pendingDrains.erase(itr);
            continue;
        }

        sBattlegroundMgr->ScheduleQueueUpdate(0, 0, itr->queueTypeId, itr->bgTypeId, itr->bracketId);
        LOG_DEBUG("module", "mod-bg-auto-queue: drain: re-scheduled queue update for bgTypeId {} bracket {} ({} uninvited waiter(s)).",
            static_cast<uint32>(itr->bgTypeId), static_cast<uint32>(itr->bracketId), waiters.total);
        ++itr;
    }
}

BgAutoQueue::QueuePassResult BgAutoQueue::RunQueuePass()
{
    // Rebuild the pool against the templates that exist right now, so every
    // pass (periodic or .bgevents run) reflects the current battlegrounds.
    ResolvePool();

    std::unordered_map<BattlegroundBracketId, BracketBucket> buckets;

    QueuePassResult result;

    for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
    {
        ++result.considered;

        SkipReason reason = SkipReason::NotInWorld;
        PvPDifficultyEntry const* bracketEntry = nullptr;
        if (!IsQueueEligible(player, &reason, &bracketEntry))
        {
            ++result.skipped[static_cast<size_t>(reason)];
            continue;
        }

        BracketBucket& bucket = buckets[bracketEntry->GetBracketId()];
        bucket.minLevel = bracketEntry->minLevel;
        bucket.maxLevel = bracketEntry->maxLevel;
        bucket.players.push_back({ player->GetGUID(), static_cast<uint8>(player->GetLevel()), player->GetTeamId() });
    }

    for (auto const& [bracketId, bucket] : buckets)
    {
        Optional<BattlegroundBracketId> liveBracket;
        BattlegroundTypeId bgTypeId = SelectBattlegroundForBracket(bucket, liveBracket);
        if (bgTypeId == BATTLEGROUND_TYPE_NONE)
        {
            LOG_DEBUG("module", "mod-bg-auto-queue: bracket {} has no eligible battleground, skipping.", static_cast<uint32>(bracketId));
            ++result.bracketsWithoutBg;
            continue;
        }

        uint32 const queued = QueueBucket(bgTypeId, bucket, liveBracket, &result.skippedAtQueueTime);
        if (queued > 0)
        {
            result.players += queued;
            result.brackets.push_back({ bucket.minLevel, bucket.maxLevel, queued });
        }
    }

    std::sort(result.brackets.begin(), result.brackets.end(),
        [](QueuePassResult::BracketCount const& a, QueuePassResult::BracketCount const& b)
        {
            return a.minLevel < b.minLevel;
        });

    LOG_INFO("module", "mod-bg-auto-queue: queue pass queued {} player(s) across {} bracket(s).", result.players, result.brackets.size());
    for (QueuePassResult::BracketCount const& bracket : result.brackets)
        LOG_INFO("module", "mod-bg-auto-queue:   bracket {}-{}: {} player(s).", bracket.minLevel, bracket.maxLevel, bracket.players);

    return result;
}

void BgAutoQueue::Update(uint32 diff)
{
    // Before the Enable/Interval gate: a `.bgevents run` backlog must drain
    // even while the periodic schedule is off.
    UpdatePendingDrains(diff);

    if (!_enabled || _intervalMs == 0)
        return;

    _elapsedMs += diff;

    uint32 const target = (_firstPass && _initialDelayMs > 0) ? _initialDelayMs : _intervalMs;

    // uint64 sum: plain (target - _elapsedMs) underflows when one stalled
    // world tick carries _elapsedMs past target, silently skipping the
    // promised warning (it then fires late, right before the pass below).
    if (!_warningSent && target > _warningLeadMs && uint64(_elapsedMs) + _warningLeadMs >= target)
    {
        BroadcastWarning();
        _warningSent = true;
    }

    if (_elapsedMs >= target)
    {
        RunQueuePass();
        _elapsedMs = 0;
        _warningSent = false;
        _firstPass = false;
    }
}

uint32 BgAutoQueue::GetTimeUntilNextPass() const
{
    if (!_enabled || _intervalMs == 0)
        return 0;

    uint32 const target = (_firstPass && _initialDelayMs > 0) ? _initialDelayMs : _intervalMs;
    return target > _elapsedMs ? (target - _elapsedMs) : 0;
}

void BgAutoQueue::BroadcastWarning() const
{
    if (_broadcastMessage.empty())
        return;

    uint32 sent = 0;
    for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
    {
        if (!IsQueueEligible(player))
            continue;

        WorldSession* session = player->GetSession();
        if (!session)
            continue;

        ChatHandler(session).SendSysMessage(_broadcastMessage);
        ++sent;
    }

    LOG_DEBUG("module", "mod-bg-auto-queue: broadcast warning sent to {} player(s).", sent);
}
