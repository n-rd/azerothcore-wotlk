/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license
 */

#include "BgAutoQueue.h"

#include "Battleground.h"
#include "Player.h"
#include "ScriptMgr.h"

class mod_bg_auto_queue_world : public WorldScript
{
public:
    mod_bg_auto_queue_world() : WorldScript("mod_bg_auto_queue_world", {
        WORLDHOOK_ON_AFTER_CONFIG_LOAD,
        WORLDHOOK_ON_UPDATE
    }) { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        sBgAutoQueue->LoadConfig();
    }

    void OnUpdate(uint32 diff) override
    {
        sBgAutoQueue->Update(diff);
    }
};

class mod_bg_auto_queue_player : public PlayerScript
{
public:
    mod_bg_auto_queue_player() : PlayerScript("mod_bg_auto_queue_player", {
        PLAYERHOOK_CAN_BATTLEFIELD_PORT,
        PLAYERHOOK_ON_BATTLEGROUND_DESERTION,
        PLAYERHOOK_ON_LOGOUT
    }) { }

    bool OnPlayerCanBattleFieldPort(Player* player, uint8 arenaType, BattlegroundTypeId bgTypeId, uint8 action) override
    {
        // Observe only; the module never vetoes the port action.
        sBgAutoQueue->HandleBattleFieldPort(player, arenaType, bgTypeId, action);
        return true;
    }

    void OnPlayerBattlegroundDesertion(Player* player, BattlegroundDesertionType const desertionType) override
    {
        // Fires before the core removes the expired entry from the queue, so
        // the tracked entry can still be identity-checked. INVITE_LOGOUT needs
        // no handling here — the logout hook covers cleanup.
        if (desertionType == BG_DESERTION_TYPE_NO_ENTER_BUTTON)
            sBgAutoQueue->HandleInviteExpired(player);
    }

    void OnPlayerLogout(Player* player) override
    {
        sBgAutoQueue->UntrackPlayer(player->GetGUID());
    }
};

void AddSC_mod_bg_auto_queue()
{
    new mod_bg_auto_queue_world();
    new mod_bg_auto_queue_player();
}
