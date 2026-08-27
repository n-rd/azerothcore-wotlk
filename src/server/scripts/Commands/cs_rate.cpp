/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Chat.h"
#include "CommandScript.h"
#include "Config.h"
#include "RBAC.h"
#include "Util.h"
#include "World.h"
#include <string>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
    struct RateDefinition
    {
        char const* Name;        // token used in the command
        ServerConfigs Config;
        char const* ConfigName;  // worldserver.conf option, used by ".rate reset"
        float DefaultValue;
    };

    RateDefinition const ServerRates[] =
    {
        { "xp.kill",             RATE_XP_KILL,                    "Rate.XP.Kill",                   1.0f },
        { "xp.quest",            RATE_XP_QUEST,                   "Rate.XP.Quest",                  1.0f },
        { "xp.explore",          RATE_XP_EXPLORE,                 "Rate.XP.Explore",                1.0f },
        { "gold",                RATE_DROP_MONEY,                 "Rate.Drop.Money",                1.0f },
        { "itemdrop.poor",       RATE_DROP_ITEM_POOR,             "Rate.Drop.Item.Poor",            1.0f },
        { "itemdrop.normal",     RATE_DROP_ITEM_NORMAL,           "Rate.Drop.Item.Normal",          1.0f },
        { "itemdrop.uncommon",   RATE_DROP_ITEM_UNCOMMON,         "Rate.Drop.Item.Uncommon",        1.0f },
        { "itemdrop.rare",       RATE_DROP_ITEM_RARE,             "Rate.Drop.Item.Rare",            1.0f },
        { "itemdrop.epic",       RATE_DROP_ITEM_EPIC,             "Rate.Drop.Item.Epic",            1.0f },
        { "itemdrop.legendary",  RATE_DROP_ITEM_LEGENDARY,        "Rate.Drop.Item.Legendary",       1.0f },
        { "itemdrop.artifact",   RATE_DROP_ITEM_ARTIFACT,         "Rate.Drop.Item.Artifact",        1.0f },
        { "itemdrop.referenced", RATE_DROP_ITEM_REFERENCED,       "Rate.Drop.Item.Referenced",      1.0f },
        { "gathering",           RATE_DROP_ITEM_GATHERING_AMOUNT, "Rate.Drop.Item.GatheringAmount", 1.0f },
    };

    constexpr float MAX_RATE_VALUE = 10000.0f;

    // Resolves a rate token to the matching definitions: an exact name ("gold", "xp.kill")
    // or a group prefix matching every "prefix." entry ("xp", "itemdrop").
    std::vector<RateDefinition const*> ResolveRateName(std::string const& name)
    {
        std::vector<RateDefinition const*> matches;
        std::string const groupPrefix = name + ".";

        for (RateDefinition const& rate : ServerRates)
        {
            if (StringEqualI(rate.Name, name))
                return { &rate };

            if (StringStartsWithI(rate.Name, groupPrefix))
                matches.push_back(&rate);
        }

        return matches;
    }
}

class rate_commandscript : public CommandScript
{
public:
    rate_commandscript() : CommandScript("rate_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable rateCommandTable =
        {
            { "set",   HandleRateSetCommand,   rbac::RBAC_PERM_COMMAND_RATE_SET,   Console::Yes },
            { "reset", HandleRateResetCommand, rbac::RBAC_PERM_COMMAND_RATE_RESET, Console::Yes },
            { "",      HandleRateListCommand,  rbac::RBAC_PERM_COMMAND_RATE,       Console::Yes }
        };

        static ChatCommandTable commandTable =
        {
            { "rate", rateCommandTable }
        };

        return commandTable;
    }

    static bool HandleRateListCommand(ChatHandler* handler)
    {
        handler->SendSysMessage("Current server rates:");
        for (RateDefinition const& rate : ServerRates)
            handler->PSendSysMessage("|- {} = {:.2f}", rate.Name, sWorld->getRate(rate.Config));

        handler->SendSysMessage("Groups: xp, itemdrop. Use .rate set $name $value and .rate reset $name (or all).");
        return true;
    }

    static bool HandleRateSetCommand(ChatHandler* handler, std::string name, float value)
    {
        std::vector<RateDefinition const*> rates = ResolveRateName(name);
        if (rates.empty())
        {
            handler->SendErrorMessage("Unknown rate '{}'. Use .rate to list the supported rates.", name);
            return false;
        }

        if (!(value > 0.0f) || value > MAX_RATE_VALUE)
        {
            handler->SendErrorMessage("Rate value must be greater than 0 and at most {}.", MAX_RATE_VALUE);
            return false;
        }

        for (RateDefinition const* rate : rates)
        {
            float oldValue = sWorld->getRate(rate->Config);
            sWorld->setRate(rate->Config, value);
            handler->PSendSysMessage("Rate {} set to {:.2f} (was {:.2f}).", rate->Name, value, oldValue);
        }

        handler->SendSysMessage("Not saved to the config file: .reload config or a restart restores it.");
        return true;
    }

    static bool HandleRateResetCommand(ChatHandler* handler, std::string name)
    {
        std::vector<RateDefinition const*> rates;
        if (StringEqualI(name, "all"))
        {
            for (RateDefinition const& rate : ServerRates)
                rates.push_back(&rate);
        }
        else
            rates = ResolveRateName(name);

        if (rates.empty())
        {
            handler->SendErrorMessage("Unknown rate '{}'. Use .rate to list the supported rates.", name);
            return false;
        }

        for (RateDefinition const* rate : rates)
        {
            float configValue = sConfigMgr->GetOption<float>(rate->ConfigName, rate->DefaultValue);
            if (!(configValue > 0.0f))
                configValue = rate->DefaultValue;

            sWorld->setRate(rate->Config, configValue);
            handler->PSendSysMessage("Rate {} restored to its configured value {:.2f}.", rate->Name, configValue);
        }

        return true;
    }
};

void AddSC_rate_commandscript()
{
    new rate_commandscript();
}
