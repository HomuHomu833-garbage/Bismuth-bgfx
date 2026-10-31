#pragma once

// "Unlock FPS": lets Geometry Dash render above the screen's refresh rate.
//
// Two things cap GD's frame rate:
//  - vsync: the swap interval of GD's GL context (1 by default on Android and
//    macOS, GD's VSync option on Windows). Set to 0 while unlocked.
//  - cocos2d's animation interval, the frame limiter of GD's main loop on
//    Windows (and Android below 60 FPS). Set to 1 / "FPS cap" while unlocked,
//    and restored when turned off.
// On Android and macOS, where the loop is otherwise only paced by vsync,
// Bismuth also sleeps to apply the cap.
//
// iOS isn't supported: every frame is driven by the system's display timer.
namespace bismuth::fps {

#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS) || defined(GEODE_IS_ANDROID)
/// Call on GD's GL thread at the start of every frame.
void beforeFrame();
/// Call at the end of every frame.
void afterFrame();
#else
inline void beforeFrame() {}
inline void afterFrame() {}
#endif

}
