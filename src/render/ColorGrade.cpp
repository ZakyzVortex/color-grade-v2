#include "render/ColorGrade.h"

#include "render/GotHook.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

namespace color_grade {

namespace {

constexpr const char *kTag = "Color Grade";
constexpr const char *kLibrary = "libminecraftpe.so";
constexpr const char *kSymbol = "eglSwapBuffers";

#define CG_LOGI(...) __android_log_print(ANDROID_LOG_INFO, kTag, __VA_ARGS__)
#define CG_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, kTag, __VA_ARGS__)

// Values are atomic because the in-game menu changes them while the render thread reads them.
struct AtomicSettings {
    std::atomic<bool> enabled{true};
    std::atomic<float> brightness{0.0f};
    std::atomic<float> contrast{1.05f};
    std::atomic<float> saturation{1.2f};
    std::atomic<float> gamma{1.0f};
    std::atomic<float> temperature{0.0f};
    std::atomic<float> vibrance{0.0f};
    std::atomic<float> vignette{0.0f};
};
AtomicSettings g_settings;

Settings loadSettings() {
    Settings s;
    s.enabled = g_settings.enabled.load(std::memory_order_relaxed);
    s.brightness = g_settings.brightness.load(std::memory_order_relaxed);
    s.contrast = g_settings.contrast.load(std::memory_order_relaxed);
    s.saturation = g_settings.saturation.load(std::memory_order_relaxed);
    s.gamma = g_settings.gamma.load(std::memory_order_relaxed);
    s.temperature = g_settings.temperature.load(std::memory_order_relaxed);
    s.vibrance = g_settings.vibrance.load(std::memory_order_relaxed);
    s.vignette = g_settings.vignette.load(std::memory_order_relaxed);
    return s;
}
void *g_originalSwap = nullptr;
std::atomic<bool> g_hooked{false};
std::atomic<bool> g_stop{false};
std::thread g_thread;

const char *kVertexShader = R"(#version 300 es
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char *kFragmentShader = R"(#version 300 es
precision highp float;
uniform sampler2D uScene;
uniform vec4 uA;   // brightness, contrast, saturation, gamma
uniform vec4 uB;   // temperature, vibrance, vignette, unused
uniform vec2 uSize;
out vec4 outColor;

void main() {
    vec3 c = texelFetch(uScene, ivec2(gl_FragCoord.xy), 0).rgb;

    c += uA.x;                               // brightness
    c = (c - 0.5) * uA.y + 0.5;              // contrast
    c.r += uB.x * 0.1;                       // temperature
    c.b -= uB.x * 0.1;

    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float sat = max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b));
    float boost = 1.0 + uB.y * (1.0 - clamp(sat, 0.0, 1.0));
    c = mix(vec3(luma), c, uA.z * boost);    // saturation + vibrance

    c = pow(max(c, vec3(0.0)), vec3(1.0 / uA.w)); // gamma

    vec2 uv = gl_FragCoord.xy / uSize;       // vignette
    c *= 1.0 - uB.z * smoothstep(0.3, 0.9, distance(uv, vec2(0.5)));

    outColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)";

bool isIdentity(const Settings &s) {
    return s.brightness == 0.0f && s.contrast == 1.0f && s.saturation == 1.0f && s.gamma == 1.0f &&
           s.temperature == 0.0f && s.vibrance == 0.0f && s.vignette == 0.0f;
}

GLuint compile(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {};
        glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
        CG_LOGE("Shader compile failed: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// Saves the GL state we touch and puts it back when it goes out of scope.
struct GlStateGuard {
    GLint program = 0, vao = 0, activeTexture = 0, texture = 0, sampler = 0;
    GLint readFbo = 0, drawFbo = 0;
    GLint viewport[4] = {};
    GLboolean colorMask[4] = {};
    GLboolean scissor, blend, depth, cull, stencil, dither, discard, a2c;

    GlStateGuard() {
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
        scissor = glIsEnabled(GL_SCISSOR_TEST);
        blend = glIsEnabled(GL_BLEND);
        depth = glIsEnabled(GL_DEPTH_TEST);
        cull = glIsEnabled(GL_CULL_FACE);
        stencil = glIsEnabled(GL_STENCIL_TEST);
        dither = glIsEnabled(GL_DITHER);
        discard = glIsEnabled(GL_RASTERIZER_DISCARD);
        a2c = glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        glGetIntegerv(GL_SAMPLER_BINDING, &sampler);
    }

    static void set(GLenum cap, GLboolean on) {
        if (on)
            glEnable(cap);
        else
            glDisable(cap);
    }

    ~GlStateGuard() {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
        glBindSampler(0, static_cast<GLuint>(sampler));
        glActiveTexture(static_cast<GLenum>(activeTexture));
        glBindVertexArray(static_cast<GLuint>(vao));
        glUseProgram(static_cast<GLuint>(program));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFbo));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFbo));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
        set(GL_SCISSOR_TEST, scissor);
        set(GL_BLEND, blend);
        set(GL_DEPTH_TEST, depth);
        set(GL_CULL_FACE, cull);
        set(GL_STENCIL_TEST, stencil);
        set(GL_DITHER, dither);
        set(GL_RASTERIZER_DISCARD, discard);
        set(GL_SAMPLE_ALPHA_TO_COVERAGE, a2c);
    }
};

class Renderer {
  public:
    void draw(EGLDisplay display, EGLSurface surface) {
        const Settings s = loadSettings();
        if (!s.enabled || isIdentity(s))
            return;

        const EGLContext context = eglGetCurrentContext();
        if (context == EGL_NO_CONTEXT)
            return;
        if (context != context_) {
            context_ = context;
            ready_ = false;
            failed_ = false;
            texWidth_ = texHeight_ = 0;
        }
        if (failed_)
            return;
        if (!ready_ && !init()) {
            failed_ = true;
            return;
        }

        EGLint width = 0, height = 0;
        eglQuerySurface(display, surface, EGL_WIDTH, &width);
        eglQuerySurface(display, surface, EGL_HEIGHT, &height);
        if (width <= 0 || height <= 0)
            return;

        GlStateGuard guard;

        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_DITHER);
        glDisable(GL_RASTERIZER_DISCARD);
        glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        // Copy the finished frame from the window surface into our texture.
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindSampler(0, 0);
        glBindTexture(GL_TEXTURE_2D, texture_);
        if (width != texWidth_ || height != texHeight_) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, width, height, 0);
            texWidth_ = width;
            texHeight_ = height;
        } else {
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
        }

        // Draw it back with the color grade applied.
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glUseProgram(program_);
        glBindVertexArray(vao_);
        glUniform1i(uScene_, 0);
        glUniform4f(uA_, s.brightness, s.contrast, s.saturation, s.gamma);
        glUniform4f(uB_, s.temperature, s.vibrance, s.vignette, 0.0f);
        glUniform2f(uSize_, static_cast<float>(width), static_cast<float>(height));
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

  private:
    bool init() {
        const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
        if (!version || std::strncmp(version, "OpenGL ES 3", 11) != 0) {
            CG_LOGE("OpenGL ES 3.x is required, got: %s", version ? version : "(null)");
            return false;
        }

        GLuint vs = compile(GL_VERTEX_SHADER, kVertexShader);
        GLuint fs = compile(GL_FRAGMENT_SHADER, kFragmentShader);
        if (!vs || !fs) {
            if (vs)
                glDeleteShader(vs);
            if (fs)
                glDeleteShader(fs);
            return false;
        }

        program_ = glCreateProgram();
        glAttachShader(program_, vs);
        glAttachShader(program_, fs);
        glLinkProgram(program_);
        glDeleteShader(vs);
        glDeleteShader(fs);

        GLint ok = 0;
        glGetProgramiv(program_, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[512] = {};
            glGetProgramInfoLog(program_, sizeof(log) - 1, nullptr, log);
            CG_LOGE("Program link failed: %s", log);
            glDeleteProgram(program_);
            program_ = 0;
            return false;
        }

        uScene_ = glGetUniformLocation(program_, "uScene");
        uA_ = glGetUniformLocation(program_, "uA");
        uB_ = glGetUniformLocation(program_, "uB");
        uSize_ = glGetUniformLocation(program_, "uSize");
        glGenVertexArrays(1, &vao_);
        glGenTextures(1, &texture_);

        ready_ = true;
        CG_LOGI("Renderer ready (%s)", version);
        return true;
    }

    EGLContext context_ = EGL_NO_CONTEXT;
    bool ready_ = false;
    bool failed_ = false;
    GLuint program_ = 0, vao_ = 0, texture_ = 0;
    GLint uScene_ = -1, uA_ = -1, uB_ = -1, uSize_ = -1;
    EGLint texWidth_ = 0, texHeight_ = 0;
};

Renderer g_renderer;

EGLBoolean hookedSwapBuffers(EGLDisplay display, EGLSurface surface) {
    g_renderer.draw(display, surface);
    auto original = reinterpret_cast<EGLBoolean (*)(EGLDisplay, EGLSurface)>(g_originalSwap);
    return original ? original(display, surface) : EGL_FALSE;
}

} // namespace

Settings sanitize(Settings s) {
    s.brightness = std::clamp(s.brightness, -0.5f, 0.5f);
    s.contrast = std::clamp(s.contrast, 0.5f, 1.5f);
    s.saturation = std::clamp(s.saturation, 0.0f, 2.0f);
    s.gamma = std::clamp(s.gamma, 0.5f, 2.0f);
    s.temperature = std::clamp(s.temperature, -1.0f, 1.0f);
    s.vibrance = std::clamp(s.vibrance, -1.0f, 1.0f);
    s.vignette = std::clamp(s.vignette, 0.0f, 1.0f);
    return s;
}

void setSettings(const Settings &settings) {
    const Settings s = sanitize(settings);
    g_settings.enabled = s.enabled;
    g_settings.brightness = s.brightness;
    g_settings.contrast = s.contrast;
    g_settings.saturation = s.saturation;
    g_settings.gamma = s.gamma;
    g_settings.temperature = s.temperature;
    g_settings.vibrance = s.vibrance;
    g_settings.vignette = s.vignette;
}

Settings currentSettings() { return loadSettings(); }

bool setParameter(std::string_view key, float value) {
    Settings s = loadSettings();
    if (key == "brightness")
        s.brightness = value;
    else if (key == "contrast")
        s.contrast = value;
    else if (key == "saturation")
        s.saturation = value;
    else if (key == "gamma")
        s.gamma = value;
    else if (key == "temperature")
        s.temperature = value;
    else if (key == "vibrance")
        s.vibrance = value;
    else if (key == "vignette")
        s.vignette = value;
    else
        return false;
    setSettings(s);
    return true;
}

void setEnabled(bool enabled) { g_settings.enabled = enabled; }

void installHook() {
    if (g_hooked.load() || g_thread.joinable())
        return;
    g_stop = false;
    g_thread = std::thread([] {
        // libminecraftpe.so may not be loaded yet when the mod is enabled, so keep trying for a while.
        for (int attempt = 0; attempt < 1200 && !g_stop; ++attempt) {
            const int patched = got::replaceSymbol(kLibrary, kSymbol, nullptr,
                                                   reinterpret_cast<void *>(&hookedSwapBuffers),
                                                   &g_originalSwap);
            if (patched > 0) {
                g_hooked = true;
                CG_LOGI("Hooked %s in %s (%d slot(s))", kSymbol, kLibrary, patched);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        if (!g_stop)
            CG_LOGE("Could not find an import of %s in %s; the color filter is inactive", kSymbol, kLibrary);
    });
}

void removeHook() {
    g_stop = true;
    if (g_thread.joinable())
        g_thread.join();
    if (g_hooked.exchange(false)) {
        got::replaceSymbol(kLibrary, kSymbol, reinterpret_cast<void *>(&hookedSwapBuffers), g_originalSwap,
                           nullptr);
        CG_LOGI("Unhooked %s", kSymbol);
    }
}

} // namespace color_grade
