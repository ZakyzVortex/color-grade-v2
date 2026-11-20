#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string_view>

#include "mod/Config.h"

#include <pl/Mod.hpp>

namespace color_grade {

class ColorGradeMod {
  public:
    static ColorGradeMod &instance();

    ColorGradeMod();

    [[nodiscard]] ll::mod::NativeMod &getSelf() const { return mSelf; }

    bool load();
    bool enable();
    bool disable();
    bool unload();

    // Called by the in-game Mod Menu.
    void onModuleToggled(bool enabled);
    void onValueChanged(std::string_view key, std::string_view value);

  private:
    bool registerMenu();
    void saveConfig(bool force);

    ll::mod::NativeMod &mSelf;
    ModConfig mConfig;
    std::optional<pl::config::ConfigFile<ModConfig>> mConfigFile;

    std::mutex mMutex;
    bool mDirty = false;
    std::chrono::steady_clock::time_point mLastSave{};
};

} // namespace color_grade
