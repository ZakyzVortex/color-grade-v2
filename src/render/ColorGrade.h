#pragma once

#include <string_view>

namespace color_grade {

struct Settings {
    bool enabled = true;
    float brightness = 0.0f;  // -0.5 .. 0.5   (added to the color)
    float contrast = 1.05f;   //  0.5 .. 1.5
    float saturation = 1.2f;  //  0.0 .. 2.0
    float gamma = 1.0f;       //  0.5 .. 2.0   (>1 brightens the midtones)
    float temperature = 0.0f; // -1.0 .. 1.0   (>0 warmer, <0 cooler)
    float vibrance = 0.0f;    // -1.0 .. 1.0   (boosts muted colors more than vivid ones)
    float vignette = 0.0f;    //  0.0 .. 1.0   (darkens the screen corners)
};

// Clamps every field to its valid range.
Settings sanitize(Settings s);

// Replaces all settings at once (values are clamped). Safe to call from any thread.
void setSettings(const Settings &settings);

// Returns the settings currently used by the renderer.
Settings currentSettings();

// Changes one value by its config key ("brightness", "contrast", ...). The value is clamped.
// Returns false if the key is unknown. Safe to call from any thread, also while the game runs.
bool setParameter(std::string_view key, float value);

void setEnabled(bool enabled);

// Starts a background thread that waits for libminecraftpe.so and redirects its eglSwapBuffers import
// to our function. Returns immediately.
void installHook();

// Stops the thread and puts the original eglSwapBuffers back.
void removeHook();

} // namespace color_grade
