#include "MpEventProcessor.h"
#include "MpLog.h"
#include "Player.h"
#include "ScriptMgr.h"

#include <boost/algorithm/string/predicate.hpp> // For starts_with
#include <string>

/**
 * This script file is a special event handler attached to the chat channel for MythicPlus to intercept
 * message events from the client hidden chat channel and process them, as well as return events back to
 * the client.  It's a very simplified version of how Eluna / AIO manage messages from UI to C++ mods.
 *
 * All Messages come into a chat channel from a specific user on a hidden channel; the protocol is defined in
 * MpEventProcessor.h.
 */

class MythicPlus_PlayerMessageEvents : public PlayerScript
{
public:
    MythicPlus_PlayerMessageEvents() : PlayerScript("MythicPlus_PlayerMessageEvents") {}

    /**
     * Listens to AddOn Chat channel for Mythic+ communication between UI and server mythic+ functionality
     */
    // Core removed the void OnChat observer hooks; OnPlayerCanUseChat is the
    // replacement with the same whisper parameters — observe and always allow.
    bool OnPlayerCanUseChat(Player* player, uint32 /*type*/, uint32 lang, std::string& msg, Player* receiver) override
    {
        // All communication from the client should be a whisper to themselves over tha addon channel
        if (!player || !receiver)
        {
            return true;
        }

        if (lang == LANG_ADDON)
        {
            if (msg.empty())
            {
                MpLog::Info(MpLog::Area::Events, "Empty AddOn message received from player: {}", player->GetName());
                return true;
            }

            // if the message begins with our prefix for our data channel then process the event
            if (boost::starts_with(msg, MP_DATA_CHAT_CHANNEL))
            {
                sMpEventProcessor->ProcessMessage(player, msg);
            }
        }

        return true;
    }
};

void Add_MP_PlayerMessageEvents()
{
    MpLog::Debug(MpLog::Area::Events, "Add_MP_PlayerEventMessages");
    new MythicPlus_PlayerMessageEvents();
}
