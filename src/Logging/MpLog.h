#ifndef MP_LOG_H
#define MP_LOG_H

#include "Log.h"
#include <string>

// Module logging. Each area is a child of "module.MythicPlus". To see one area's DEBUG output, add to
// env/dist/etc/worldserver.conf (Logger keys can't be set via AC_* env vars):
//   Logger.module.MythicPlus.scaling=5,Server
// DEBUG goes to Server.log; the console (docker logs) is capped at INFO.
namespace MpLog
{
    enum class Area { Config, Instance, Scaling, Loot, Advancement, Events, Combat };

    inline std::string const& Logger(Area area)
    {
        static std::string const names[] = {
            "module.MythicPlus.config", "module.MythicPlus.instance", "module.MythicPlus.scaling",
            "module.MythicPlus.loot", "module.MythicPlus.advancement", "module.MythicPlus.events",
            "module.MythicPlus.combat"
        };
        return names[static_cast<int>(area)];
    }

    inline bool Enabled(Area area, LogLevel level) { return sLog->ShouldLog(Logger(area), level); }

    template<typename... Args>
    void Debug(Area area, Acore::FormatStringView fmt, Args&&... args)
    { LOG_DEBUG(Logger(area), fmt, std::forward<Args>(args)...); }

    template<typename... Args>
    void Info(Area area, Acore::FormatStringView fmt, Args&&... args)
    { LOG_INFO(Logger(area), fmt, std::forward<Args>(args)...); }

    template<typename... Args>
    void Warn(Area area, Acore::FormatStringView fmt, Args&&... args)
    { LOG_WARN(Logger(area), fmt, std::forward<Args>(args)...); }

    template<typename... Args>
    void Error(Area area, Acore::FormatStringView fmt, Args&&... args)
    { LOG_ERROR(Logger(area), fmt, std::forward<Args>(args)...); }
}

#endif
