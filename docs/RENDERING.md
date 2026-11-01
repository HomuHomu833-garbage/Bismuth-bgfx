# Bismuth rendering architecture (bgfx)

Bismuth renders Geometry Dash's level objects itself, using
[bgfx](https://github.com/bkaradzic/bgfx), on Windows, macOS, iOS and Android
(and Linux, where Geometry Dash runs as the Windows build under Wine/Proton).

## Backends

Geometry Dash always renders with OpenGL (cocos2d-x), and Bismuth's layers must
be drawn *between* cocos2d's own nodes (players, particles, effects...). There
are two ways to do that:

| Backend | Platforms | How |
| --- | --- | --- |
| **Vulkan** (default) | Windows, Android | bgfx renders offscreen into textures shared with GD's GL context, which composites them at the right z-order. |
| **Metal** (default) | macOS, iOS | Same, with IOSurface-backed textures. |
| **OpenGL** (fallback) | all | bgfx renders directly inside GD's own GL context, flushing at every point where Bismuth draws. |

With the `Auto` setting, Vulkan/Metal is tried first and OpenGL is used if it
can't start (no driver support, no GL/Vulkan sharing, a different GPU, ...).
The reasons are logged and shown in the F3 overlay.

Sharing details:

| Platform | Memory | Synchronization |
| --- | --- | --- |
| Windows | `VK_KHR_external_memory_win32` + `GL_EXT_memory_object_win32` | `GL_EXT_semaphore_win32` (CPU wait fallback) |
| Android | `AHardwareBuffer` (Vulkan import + EGLImage) | sync fds + `EGL_ANDROID_native_fence_sync` (CPU wait fallback) |
| macOS | IOSurface + `CGLTexImageIOSurface2D` | GL flush / Metal "scheduled" ordering |
| iOS | CVPixelBuffer + CoreVideo texture caches | same as macOS |
| Linux (tests) | fd memory objects | `GL_EXT_semaphore_fd` (CPU wait fallback) |

On Windows the Vulkan device is picked by matching the GL driver's device UUID,
so dual-GPU laptops share textures on the same GPU.

### Composite groups

Consecutive Bismuth layers with no other visible cocos2d node in between form a
*composite group*. A group is drawn in one go (one flush with OpenGL, one shared
texture with Vulkan/Metal). Normal layers (premultiplied "over") and additive
layers (`SRC_ALPHA, ONE`) are accumulated so that compositing the group with
`ONE, ONE_MINUS_SRC_ALPHA` gives the same result as drawing every sprite
directly (additive sprites leave the group's alpha untouched).

## Culling

Every frame, all sprites are checked against the camera, transformed by their
group (move/rotate triggers) and colored (color channels, HSV, pulses, invisible
blocks, audio scale).

- **GPU culling** (compute shaders): `cs_cull` processes work groups of sprites
  and writes visible triangles to private ranges, `cs_scan` computes per-layer
  offsets and writes one indirect draw per layer, `cs_copy` compacts the
  triangles. The vertex shader pulls vertices from storage buffers.
- **CPU culling**: the same work on all CPU cores, producing a vertex buffer.
  Used when compute shaders aren't available (e.g. OpenGL fallback on macOS,
  iOS and Android, old GPUs), or when forced in the settings.

With OpenGL and CPU culling, sprites aren't drawn through bgfx but with a few
raw GL calls per layer (`src/gfx/HostSpriteRenderer.cpp`), restoring only the
state they change: a bgfx frame per composite group costs a lot where GL calls
are expensive (Wine + Box64 on ARM, drivers running GL on a separate thread).
The overdraw view still uses bgfx.

Both use the same code: `shaders/bismuth_kernel.sh` is written in a subset that
compiles as GLSL, as bgfx's HLSL-based shader language (SPIR-V, Metal) and as
C++ (with glm). `tests/render_test.cpp` checks that every backend and culling
combination produces the same image.

## Settings

| Setting | |
| --- | --- |
| Renderer backend | Auto / Vulkan / Metal / OpenGL |
| Culling mode | Auto / GPU / CPU |
| GPU core count | GPU culling work group sizing (auto-detected on Windows only) |
| Show debug overlay | Same as F3, for devices without a keyboard |
| Overdraw view | Same as F4 |
| Unlock FPS | Windows, macOS, Android: turns off vsync and GD's frame limiter |
| FPS cap | Frame rate limit while unlocked (0 = none) |

## Building

The mod builds with the Geode SDK as usual (`geode build`). bgfx is fetched at a
pinned revision and patched (`cmake/Bgfx.cmake`, `cmake/patches/`). That
revision (2023-05) is the last one that supports OpenGL on macOS/iOS, which is
required for the fallback there; the patch adds embedding in a host GL context,
Vulkan texture/semaphore sharing and headless Metal.

Shaders are compiled ahead of time and committed in `src/gfx/shaders/generated`.
After editing `shaders/`, regenerate them with:

```bash
tools/build_shaderc.sh
```

Rendering core tests (Linux, Mesa llvmpipe + lavapipe):

```bash
cmake -S tests -B build-tests -G Ninja && cmake --build build-tests
./build-tests/bismuth_kernel_test && xvfb-run -a ./build-tests/bismuth_render_test
```

## Debug tools

- F3: debug overlay (backend, sharing method, culling path, GPU time).
- F4: overdraw view. Counts Bismuth's layers only (the original stencil-based
  version also counted cocos2d's draws, which can't be read back on every backend).
- F8: toggle Bismuth (with "Enable debug tools").
- F9: difference mode (vanilla vs Bismuth).

## Known limitations

- Vulkan/Metal need one screen-sized RGBA8 texture per composite group.
- If Android destroys the GL context mid-level (app backgrounded), Bismuth
  switches that level to vanilla rendering and restarts at the next level.
- The ESSL 3.1 compute path isn't built: the OpenGL ES fallback always uses CPU culling.
