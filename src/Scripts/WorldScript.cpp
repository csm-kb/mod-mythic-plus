#include "MpBots.h"
#include "MpConfig.h"
#include "MpRepository.h"
#include "AdvancementMgr.h"
#include "MpLog.h"
#include "Player.h"
#include "ScriptMgr.h"

void MP_Register_EventHandlers();

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
        // Registering event handlers for the Mythic+ events from client
        MP_Register_EventHandlers();
        MpLog::Info(MpLog::Area::Config, "Registered Mythic+ Event Handlers...");
    }

    // Runs before the world's --dry-run exit, so a dry run exercises the table loads
    void OnBeforeWorldInitialized() override
    {
        int32 scaleFactors = sMpRepo->LoadScaleFactors();
        int32 ranks = sAdvancementMgr->LoadAdvancementRanks();
        int32 materials = sAdvancementMgr->LoadMaterialTypes();
        sMpRepo->LoadPlayerHealthAvg();

        uint32 warnings = sMpConfig->GetWarningCount();
        if (warnings)
            MpLog::Warn(MpLog::Area::Config,
                "event=module_loaded enabled={} bots={} scale_factors={} advancement_ranks={} material_types={} "
                "config_warnings={} invalid_keys={}", sMpConfig->enabled, MpBots::ProvidersString(),
                scaleFactors, ranks, materials, warnings, sMpConfig->GetInvalidKeys());
        else
            MpLog::Info(MpLog::Area::Config,
                "event=module_loaded enabled={} bots={} scale_factors={} advancement_ranks={} material_types={} "
                "config_warnings=0", sMpConfig->enabled, MpBots::ProvidersString(), scaleFactors, ranks, materials);
    }
};

void Add_MP_WorldScripts()
{
    MpLog::Debug(MpLog::Area::Config, "Add_MP_WorldScripts()");
    new MythicPlus_WorldScript();
}
