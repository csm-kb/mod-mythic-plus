#include "MpConfig.h"
#include "MythicPlus.h"
#include "MpRepository.h"
#include "AdvancementMgr.h"
#include "MpLog.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "MpEventHandlers.cpp"

class MythicPlus_WorldScript : public WorldScript
{
public:
    MythicPlus_WorldScript() : WorldScript("MythicPlus_WorldScript") { }

    void OnAfterConfigLoad(bool reload) override
    {
        sMpConfig->Load();
        if (reload)
        {
            uint32 warnings = sMpConfig->GetWarningCount();
            if (warnings)
                MpLog::Warn(MpLog::Area::Config, "event=config_reloaded config_warnings={} invalid_keys={}",
                    warnings, sMpConfig->GetInvalidKeys());
            else
                MpLog::Info(MpLog::Area::Config, "event=config_reloaded config_warnings=0");
        }
    }

    void OnStartup() override
    {
        int32 size = sMpRepo->LoadScaleFactors();
        MpLog::Info(MpLog::Area::Config, "Loaded {} Mythic+ Scaling Factors from database...", size);

        size = sAdvancementMgr->LoadAdvancementRanks();
        MpLog::Info(MpLog::Area::Config, "Loaded {} advancement ranks...", size);

        size = sAdvancementMgr->LoadMaterialTypes();
        MpLog::Info(MpLog::Area::Config, "Loaded {} material types...", size);

        sMpRepo->LoadPlayerHealthAvg();
        MpLog::Info(MpLog::Area::Config, "Loaded player health averages used for scaling calculations...");

        // Registering event handlers for the Mythic+ events from client
        MP_Register_EventHandlers();
        MpLog::Info(MpLog::Area::Config, "Registered Mythic+ Event Handlers...");
    }
};

void Add_MP_WorldScripts()
{
    MpLog::Debug(MpLog::Area::Config, "Add_MP_WorldScripts()");
    new MythicPlus_WorldScript();
}
