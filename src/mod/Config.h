#pragma once

#include <string>
#include <string_view>

#include <pl/Config.hpp>

namespace color_grade {

struct ModConfig {
    int version = 1;
    bool enabled = true;
    float brightness = 0.0f;
    float contrast = 1.05f;
    float saturation = 1.2f;
    float gamma = 1.0f;
    float temperature = 0.0f;
    float vibrance = 0.0f;
    float vignette = 0.0f;
};

nlohmann::json makeDefaultConfigJson();
nlohmann::json makeConfigSchemaJson();

} // namespace color_grade

template <> struct pl::config::Schema<color_grade::ModConfig> {
    static constexpr std::string_view title = "Color Grade";
    static constexpr std::string_view description = "Screen color adjustments. They can also be changed live in the in-game Mod Menu.";

    static constexpr FieldSchema field(std::string_view name) {
        if (name == "version")
            return {.title = "Version", .readOnly = true};
        if (name == "enabled")
            return {.title = "Enabled", .description = "Turns the color filter on or off."};
        if (name == "brightness")
            return {.title = "Brightness", .description = "-0.5 to 0.5. 0 = unchanged."};
        if (name == "contrast")
            return {.title = "Contrast", .description = "0.5 to 1.5. 1 = unchanged."};
        if (name == "saturation")
            return {.title = "Saturation", .description = "0 to 2. 1 = unchanged, 0 = grayscale."};
        if (name == "gamma")
            return {.title = "Gamma", .description = "0.5 to 2. Above 1 brightens the midtones."};
        if (name == "temperature")
            return {.title = "Temperature", .description = "-1 (cooler) to 1 (warmer). 0 = unchanged."};
        if (name == "vibrance")
            return {.title = "Vibrance", .description = "-1 to 1. Boosts muted colors more than vivid ones."};
        if (name == "vignette")
            return {.title = "Vignette", .description = "0 to 1. Darkens the screen corners."};
        return {};
    }
};
