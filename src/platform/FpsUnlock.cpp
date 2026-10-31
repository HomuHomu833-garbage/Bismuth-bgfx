#include "FpsUnlock.hpp"

#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS) || defined(GEODE_IS_ANDROID)

#include <Geode/Geode.hpp>

#include <chrono>
#include <cstdint>
#include <thread>

#if defined(GEODE_IS_WINDOWS)
#   include <windows.h>
#elif defined(GEODE_IS_MACOS)
#   include <OpenGL/OpenGL.h>
#elif defined(GEODE_IS_ANDROID)
#   include <EGL/egl.h>
#endif

using namespace geode::prelude;

namespace bismuth::fps {

namespace {

using Clock = std::chrono::steady_clock;

bool settingsLoaded = false;
bool unlock = false;
int64_t  fpsCap = 0;

// Swap interval last set, per GL context / surface (Android recreates its
// surface when the app is resumed, which resets the swap interval).
void* appliedTarget = nullptr;
int appliedInterval = -1;

// GD's own animation interval, restored when unlocking is turned off.
double originalAnimationInterval = 0.0;
double appliedAnimationInterval = 0.0;

Clock::time_point nextFrame {};

void loadSettings() {
    settingsLoaded = true;

    auto mod = Mod::get();
    unlock = mod->getSettingValue<bool>("unlock_fps");
    fpsCap = mod->getSettingValue<int64_t>("fps_cap");

    listenForSettingChanges<bool>("unlock_fps", [](bool value) { unlock = value; });
    listenForSettingChanges<int64_t>("fps_cap", [](int64_t value) { fpsCap = value; });
}

// Platform swap interval ------------------------------------------------------

#if defined(GEODE_IS_WINDOWS)

using PFN_wglSwapIntervalEXT = BOOL (WINAPI*)(int);
using PFN_wglGetSwapIntervalEXT = int (WINAPI*)();

void* currentTarget() {
    return (void*)wglGetCurrentContext();
}

int queryInterval() {
    static auto getInterval = (PFN_wglGetSwapIntervalEXT)(void*)wglGetProcAddress("wglGetSwapIntervalEXT");
    return getInterval ? getInterval() : 1;
}

bool setInterval(int interval) {
    static auto swapInterval = (PFN_wglSwapIntervalEXT)(void*)wglGetProcAddress("wglSwapIntervalEXT");
    return swapInterval && swapInterval(interval);
}

#elif defined(GEODE_IS_MACOS)

void* currentTarget() {
    return (void*)CGLGetCurrentContext();
}

int queryInterval() {
    GLint interval = 1;
    CGLGetParameter(CGLGetCurrentContext(), kCGLCPSwapInterval, &interval);
    return interval;
}

bool setInterval(int interval) {
    GLint value = interval;
    return CGLSetParameter(CGLGetCurrentContext(), kCGLCPSwapInterval, &value) == kCGLNoError;
}

#elif defined(GEODE_IS_ANDROID)

void* currentTarget() {
    if (eglGetCurrentDisplay() == EGL_NO_DISPLAY)
        return nullptr;
    EGLSurface surface = eglGetCurrentSurface(EGL_DRAW);
    return surface == EGL_NO_SURFACE ? nullptr : (void*)surface;
}

int queryInterval() {
    return 1; // GLSurfaceView's default; EGL can't query it.
}

bool setInterval(int interval) {
    return eglSwapInterval(eglGetCurrentDisplay(), interval) == EGL_TRUE;
}

#endif

// GD's own vsync setting, to restore when unlocking is turned off.
int originalInterval = 1;

void applySwapInterval() {
    void* target = currentTarget();
    if (!target)
        return;

    if (target != appliedTarget) {
        // New context / surface: nothing applied on it yet.
        appliedTarget = target;
        appliedInterval = -1;
    }

    if (!unlock && appliedInterval == -1)
        return; // Never unlocked here: leave GD's vsync alone.

    int wanted = unlock ? 0 : originalInterval;
    if (wanted == appliedInterval)
        return;

    if (appliedInterval == -1)
        originalInterval = queryInterval();

    if (setInterval(wanted))
        log::info("Bismuth: swap interval set to {}", wanted);
    else
        log::warn("Bismuth: couldn't set the swap interval to {}, vsync stays on", wanted);

    // Remembered even on failure so it isn't retried every frame.
    appliedInterval = unlock ? wanted : -1;
}

// Animation interval ----------------------------------------------------------

void applyAnimationInterval() {
    double wanted;
    if (unlock)
        wanted = 1.0 / (double)(fpsCap > 0 ? fpsCap : 10000);
    else if (appliedAnimationInterval != 0.0)
        wanted = originalAnimationInterval;
    else
        return; // Never changed: leave GD's value alone.

    if (unlock && wanted == appliedAnimationInterval)
        return;

    auto director = CCDirector::get();
    if (appliedAnimationInterval == 0.0)
        originalAnimationInterval = director->getAnimationInterval();

    director->setAnimationInterval(wanted);
    appliedAnimationInterval = unlock ? wanted : 0.0;
}

} // namespace

void beforeFrame() {
    if (!settingsLoaded)
        loadSettings();

    applySwapInterval();
    applyAnimationInterval();
}

void afterFrame() {
#if defined(GEODE_IS_WINDOWS)
    // GD's Windows main loop already waits for the animation interval.
#else
    if (!unlock || fpsCap <= 0) {
        nextFrame = {};
        return;
    }

    auto period = std::chrono::nanoseconds(1'000'000'000LL / fpsCap);
    auto now = Clock::now();

    // Start over after a stall (loading, app in background) instead of
    // rushing frames to catch up.
    if (nextFrame == Clock::time_point {} || now - nextFrame > period * 4)
        nextFrame = now;

    nextFrame += period;
    if (nextFrame > now)
        std::this_thread::sleep_until(nextFrame);
#endif
}

}

#endif
