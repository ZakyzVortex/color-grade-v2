#include "mod/MyMod.h"

#include "render/ColorGrade.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include <pl/ModMenu.hpp>
#include <pl/Mod.hpp>

namespace color_grade {

namespace {

constexpr const char *kModuleId = "color_grade.main";

std::string num(float v) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(v));
    return buffer;
}

// Callbacks used by the Mod Menu. They forward to the single mod instance.
void onModuleToggle(std::string_view, bool enabled) { ColorGradeMod::instance().onModuleToggled(enabled); }

void onModuleConfigChanged(std::string_view, std::string_view key, std::string_view value) {
    ColorGradeMod::instance().onValueChanged(key, value);
}

} // namespace

ColorGradeMod &ColorGradeMod::instance() {
    static ColorGradeMod instance;
    return instance;
}

ColorGradeMod::ColorGradeMod() : mSelf(*ll::mod::NativeMod::current()) {}

bool ColorGradeMod::load() {
    auto &self = getSelf();
    self.getLogger().debug("Loading...");

    std::error_code ec;
    std::filesystem::create_directories(self.getDataDir(), ec);
    if (ec) {
        self.getLogger().error("Failed to create data directory {}: {}", self.getDataDir().string(),
                               ec.message());
        return false;
    }

    std::filesystem::create_directories(self.getConfigDir(), ec);
    if (ec) {
        self.getLogger().error("Failed to create config directory {}: {}",
                               self.getConfigDir().string(), ec.message());
        return false;
    }

    mConfigFile.emplace();
    if (!mConfigFile->load()) {
        self.getLogger().warn("Failed to load typed config");
        return false;
    }
    mConfig = mConfigFile->value();

    self.getLogger().info("Loaded {} from {}", self.getName(), self.getModDir().string());
    return true;
}

bool ColorGradeMod::registerMenu() {
    using pl::modmenu::ConfigType;

    return pl::modmenu::ModuleBuilder(kModuleId, "Color Grade")
        .modId(getSelf().getId())
        .description("Brightness, contrast, saturation and other screen color adjustments.")
        .defaultEnabled(mConfig.enabled)
        .onToggle(onModuleToggle)
        .onConfigChanged(onModuleConfigChanged)
        .config("brightness", "Brightness", ConfigType::SliderFloat, num(mConfig.brightness), "-0.5", "0.5")
        .config("contrast", "Contrast", ConfigType::SliderFloat, num(mConfig.contrast), "0.5", "1.5")
        .config("saturation", "Saturation", ConfigType::SliderFloat, num(mConfig.saturation), "0", "2")
        .config("gamma", "Gamma", ConfigType::SliderFloat, num(mConfig.gamma), "0.5", "2")
        .config("temperature", "Temperature", ConfigType::SliderFloat, num(mConfig.temperature), "-1", "1")
        .config("vibrance", "Vibrance", ConfigType::SliderFloat, num(mConfig.vibrance), "-1", "1")
        .config("vignette", "Vignette", ConfigType::SliderFloat, num(mConfig.vignette), "0", "1")
        .registerModule();
}

bool ColorGradeMod::enable() {
    auto &self = getSelf();
    self.getLogger().debug("Enabling...");

    Settings settings;
    settings.enabled = mConfig.enabled;
    settings.brightness = mConfig.brightness;
    settings.contrast = mConfig.contrast;
    settings.saturation = mConfig.saturation;
    settings.gamma = mConfig.gamma;
    settings.temperature = mConfig.temperature;
    settings.vibrance = mConfig.vibrance;
    settings.vignette = mConfig.vignette;
    setSettings(settings);

    installHook();

    if (!registerMenu())
        self.getLogger().warn("Could not register the Mod Menu module; edit the values in the mod details "
                              "in LeviLauncher instead");
    else
        self.getLogger().info("Color Grade menu registered");
    return true;
}

void ColorGradeMod::onModuleToggled(bool enabled) {
    setEnabled(enabled);
    {
        std::lock_guard lock(mMutex);
        mConfig.enabled = enabled;
        mDirty = true;
    }
    saveConfig(true);
}

void ColorGradeMod::onValueChanged(std::string_view key, std::string_view value) {
    const float parsed = std::strtof(std::string(value).c_str(), nullptr);
    if (!setParameter(key, parsed))
        return;

    {
        std::lock_guard lock(mMutex);
        const Settings s = currentSettings();
        mConfig.brightness = s.brightness;
        mConfig.contrast = s.contrast;
        mConfig.saturation = s.saturation;
        mConfig.gamma = s.gamma;
        mConfig.temperature = s.temperature;
        mConfig.vibrance = s.vibrance;
        mConfig.vignette = s.vignette;
        mDirty = true;
    }
    saveConfig(false); // sliders fire many times; the file is written at most about once per 1.5 s
}

void ColorGradeMod::saveConfig(bool force) {
    std::lock_guard lock(mMutex);
    if (!mDirty || !mConfigFile)
        return;
    const auto now = std::chrono::steady_clock::now();
    if (!force && now - mLastSave < std::chrono::milliseconds(1500))
        return;
    mConfigFile->value() = mConfig;
    mConfigFile->save();
    mLastSave = now;
    mDirty = false;
}

bool ColorGradeMod::disable() {
    getSelf().getLogger().debug("Disabling...");
    pl::modmenu::unregisterModule(kModuleId);
    saveConfig(true);
    removeHook();
    return true;
}

bool ColorGradeMod::unload() {
    getSelf().getLogger().debug("Unloading...");
    mConfigFile.reset();
    return true;
}

} // namespace color_grade
