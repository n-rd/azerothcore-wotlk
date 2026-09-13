/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license
 */

#include "BgAutoQueue.h"

#include "Chat.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "Util.h"

using namespace Acore::ChatCommands;

class bg_auto_queue_commandscript : public CommandScript
{
public:
    bg_auto_queue_commandscript() : CommandScript("bg_auto_queue_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable bgAutoQueueTable =
        {
            { "run", HandleBgAutoQueueRunCommand, SEC_GAMEMASTER, Console::Yes },
            { "",    HandleBgAutoQueueCommand,    SEC_PLAYER,     Console::No  },
        };

        static ChatCommandTable commandTable =
        {
            { "bgevents", bgAutoQueueTable },
        };

        return commandTable;
    }

    // Default subcommand: `.bgevents on`/`off` toggles opt state; `.bgevents`
    // with no argument prints the current state and time to the next event.
    static bool HandleBgAutoQueueCommand(ChatHandler* handler, Optional<bool> enable)
    {
        Player* player = handler->GetPlayer();
        if (!player)
        {
            handler->SendErrorMessage("This command must be used in-game.");
            return false;
        }

        if (enable.has_value())
        {
            // enable == true means opt IN, i.e. not opted out.
            sBgAutoQueue->SetOptOut(player, !*enable);

            if (*enable)
                handler->SendSysMessage("Battleground events enabled for your character.");
            else
                handler->SendSysMessage("Battleground events disabled for your character. You will not be auto-queued. Use .bgevents on to opt back in.");

            return true;
        }

        BgAutoQueue::SkipReason reason = BgAutoQueue::SkipReason::NotInWorld;
        bool const eligible = sBgAutoQueue->IsQueueEligible(player, &reason);

        // An automatic opt-out (after several declined events) reads
        // differently from a manual .bgevents off, so the player knows they
        // never asked for it. GetPlayerSkipReasonMessage has no player
        // context, hence the special case here.
        auto skipReasonMessage = [player](BgAutoQueue::SkipReason skipReason) -> char const*
        {
            if (skipReason == BgAutoQueue::SkipReason::OptedOut && sBgAutoQueue->IsAutoOptedOut(player))
                return "they were turned off automatically after you declined several events in a row. Use \".bgevents on\" to opt back in.";
            return GetPlayerSkipReasonMessage(skipReason);
        };

        uint32 const msToNext = sBgAutoQueue->GetTimeUntilNextPass();
        bool const eventsScheduled = sBgAutoQueue->IsEnabled() && msToNext > 0;

        if (!eventsScheduled)
        {
            handler->SendSysMessage("Battleground automatic events are currently turned OFF on this server. An administrator can still trigger one manually.");
            if (!eligible)
                handler->PSendSysMessage("Battleground events are DISABLED for your character because {}", skipReasonMessage(reason));
            return true;
        }

        if (eligible)
            handler->SendSysMessage("Battleground events are ENABLED for your character. You will be auto-queued at the next event.");
        else
            handler->PSendSysMessage("Battleground events are DISABLED for your character because {}", skipReasonMessage(reason));

        handler->PSendSysMessage("Next scheduled event in {}.", secsToTimeString(msToNext / 1000));
        return true;
    }

    static bool HandleBgAutoQueueRunCommand(ChatHandler* handler)
    {
        // Fires an immediate queue pass regardless of Enable/Interval without touching the periodic timer
        BgAutoQueue::QueuePassResult result = sBgAutoQueue->RunQueuePass();

        if (result.players == 0)
        {
            handler->PSendSysMessage("Battleground event: no players were queued (examined {} online player(s)).", result.considered);
            ReportSkips(handler, result);
            return true;
        }

        handler->SendSysMessage("Battleground event queued players:");
        for (BgAutoQueue::QueuePassResult::BracketCount const& bracket : result.brackets)
        {
            if (bracket.minLevel == bracket.maxLevel)
                handler->PSendSysMessage("  {}: {} player(s)", bracket.minLevel, bracket.players);
            else
                handler->PSendSysMessage("  {}-{}: {} player(s)", bracket.minLevel, bracket.maxLevel, bracket.players);
        }

        handler->PSendSysMessage("Queued {} player(s) in total", result.players);
        ReportSkips(handler, result);
        return true;
    }

private:
    // Second-person wording for the .bgevents (no-arg) player report. Distinct
    // from GetSkipReasonLabel, which is operator-flavored copy for .bgevents run.
    // Each clause ends with its own period; the format string adds none.
    static char const* GetPlayerSkipReasonMessage(BgAutoQueue::SkipReason reason)
    {
        switch (reason)
        {
            case BgAutoQueue::SkipReason::OptedOut:            return "you opted out. Use \".bgevents on\" to opt back in.";
            case BgAutoQueue::SkipReason::Level:               return "your level is outside the range set for battleground events.";
            case BgAutoQueue::SkipReason::Dungeon:             return "you are in a dungeon or raid.";
            case BgAutoQueue::SkipReason::InBattleground:      return "you are already in a battleground.";
            case BgAutoQueue::SkipReason::AlreadyQueued:       return "you are already queued elsewhere (or have no free queue slot).";
            case BgAutoQueue::SkipReason::Deserter:            return "you have the Deserter debuff or cannot currently join a battleground.";
            case BgAutoQueue::SkipReason::Lfg:                 return "you are using the LFG (Dungeon Finder) system.";
            case BgAutoQueue::SkipReason::DeathKnightEbonHold: return "your Death Knight has not yet left Ebon Hold.";
            case BgAutoQueue::SkipReason::GameMaster:          return "you are in GM mode.";
            case BgAutoQueue::SkipReason::Afk:                 return "you are flagged AFK.";
            case BgAutoQueue::SkipReason::Aura:                return "you have an aura that excludes you from battleground events.";
            case BgAutoQueue::SkipReason::NoBracket:           return "there is no battleground bracket for your level.";
            default:                                           return "you are not currently eligible for battleground events.";
        }
    }

    // Prints why online players were not queued, so an operator can tell an
    // opted-out character from a wrong-level one (etc.) without reading logs.
    static void ReportSkips(ChatHandler* handler, BgAutoQueue::QueuePassResult const& result)
    {
        bool any = false;
        for (size_t i = 0; i < result.skipped.size(); ++i)
        {
            if (result.skipped[i] == 0)
                continue;

            if (!any)
            {
                handler->SendSysMessage("Skipped players:");
                any = true;
            }

            BgAutoQueue::SkipReason reason = static_cast<BgAutoQueue::SkipReason>(i);
            handler->PSendSysMessage("  {}: {} player(s)", BgAutoQueue::GetSkipReasonLabel(reason), result.skipped[i]);
        }

        if (result.bracketsWithoutBg > 0)
            handler->PSendSysMessage("  no eligible battleground (level range, disables, or empty pool): {} bracket(s)", result.bracketsWithoutBg);

        if (result.skippedAtQueueTime > 0)
            handler->PSendSysMessage("  dropped at queue time (state change, veto, or level/bracket mismatch): {} player(s)", result.skippedAtQueueTime);
    }
};

void AddSC_bg_auto_queue_commandscript()
{
    new bg_auto_queue_commandscript();
}
