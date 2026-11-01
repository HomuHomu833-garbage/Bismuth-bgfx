#include "HostGL.hpp"

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

#if defined(_WIN32)
#   define WIN32_LEAN_AND_MEAN
#   define NOMINMAX
#   include <windows.h>
#elif defined(__ANDROID__) || (defined(__linux__) && !defined(BISMUTH_HOST_GLX))
#   include <dlfcn.h>
#   include <EGL/egl.h>
#elif defined(__APPLE__)
#   include <dlfcn.h>
#else
#   include <dlfcn.h>
#endif

namespace bismuth::gfx {

#if defined(__APPLE__)
// HostGL_apple.mm
void* appleCurrentGLContext();
#endif

//// PROC ADDRESS ////

#if defined(_WIN32)

static HMODULE openGL32() {
    static HMODULE module = [] {
        HMODULE m = GetModuleHandleA("opengl32.dll");
        return m ? m : LoadLibraryA("opengl32.dll");
    }();
    return module;
}

template <typename T>
static T win32Proc(const char* name) {
    return reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(openGL32(), name)));
}

void* getGLProcAddress(const char* name) {
    using WglGetProcAddress = PROC (WINAPI*)(LPCSTR);
    static auto wglGetProc = win32Proc<WglGetProcAddress>("wglGetProcAddress");

    void* ptr = wglGetProc ? reinterpret_cast<void*>(wglGetProc(name)) : nullptr;
    // wglGetProcAddress may return these sentinel values on failure.
    auto value = reinterpret_cast<intptr_t>(ptr);
    if (value == 0 || value == 1 || value == 2 || value == 3 || value == -1)
        ptr = reinterpret_cast<void*>(GetProcAddress(openGL32(), name));
    return ptr;
}

void* currentNativeGLContext() {
    using WglGetCurrentContext = HGLRC (WINAPI*)();
    static auto get = win32Proc<WglGetCurrentContext>("wglGetCurrentContext");
    return get ? reinterpret_cast<void*>(get()) : nullptr;
}

static void* currentNativeWindow() {
    using WglGetCurrentDC = HDC (WINAPI*)();
    static auto get = win32Proc<WglGetCurrentDC>("wglGetCurrentDC");
    HDC dc = get ? get() : nullptr;
    return dc ? reinterpret_cast<void*>(WindowFromDC(dc)) : nullptr;
}

#elif defined(__ANDROID__) || (defined(__linux__) && !defined(BISMUTH_HOST_GLX))

void* getGLProcAddress(const char* name) {
    static void* gles = [] {
        void* lib = dlopen("libGLESv3.so", RTLD_NOW | RTLD_LOCAL);
        if (!lib) lib = dlopen("libGLESv2.so", RTLD_NOW | RTLD_LOCAL);
#if defined(__linux__) && !defined(__ANDROID__)
        if (!lib) lib = dlopen("libGL.so.1", RTLD_NOW | RTLD_LOCAL);
#endif
        return lib;
    }();

    void* ptr = gles ? dlsym(gles, name) : nullptr;
    if (!ptr)
        ptr = reinterpret_cast<void*>(eglGetProcAddress(name));
    return ptr;
}

void* currentNativeGLContext() {
    return reinterpret_cast<void*>(eglGetCurrentContext());
}

static void* currentNativeWindow() { return nullptr; }

#elif defined(__APPLE__)

void* getGLProcAddress(const char* name) {
    return dlsym(RTLD_DEFAULT, name);
}

void* currentNativeGLContext() {
    return appleCurrentGLContext();
}

static void* currentNativeWindow() { return nullptr; }

#else // Linux GLX (tests)

static void* libGL() {
    static void* lib = dlopen("libGL.so.1", RTLD_NOW | RTLD_GLOBAL);
    return lib;
}

void* getGLProcAddress(const char* name) {
    using GetProc = void* (*)(const GLubyte*);
    static auto getProc = reinterpret_cast<GetProc>(dlsym(libGL(), "glXGetProcAddressARB"));
    void* ptr = getProc ? getProc(reinterpret_cast<const GLubyte*>(name)) : nullptr;
    return ptr ? ptr : dlsym(libGL(), name);
}

void* currentNativeGLContext() {
    using GetCurrent = void* (*)();
    static auto get = reinterpret_cast<GetCurrent>(dlsym(libGL(), "glXGetCurrentContext"));
    return get ? get() : nullptr;
}

static void* currentNativeWindow() { return nullptr; }

void* currentGLXDisplay() {
    using GetDisplay = void* (*)();
    static auto get = reinterpret_cast<GetDisplay>(dlsym(libGL(), "glXGetCurrentDisplay"));
    return get ? get() : nullptr;
}

#endif

//// FUNCTION TABLE ////

static GLFunctions s_functions;
static void* s_functionsContext = nullptr;
static bool s_functionsValid = false;

template <typename T>
static bool loadProc(T& out, const char* name) {
    out = reinterpret_cast<T>(getGLProcAddress(name));
    return out != nullptr;
}

template <typename T>
static bool loadProcWithSuffixes(T& out, const char* name) {
    if (loadProc(out, name))
        return true;
    // GL 2.1 / ES 2.0 contexts expose some entry points as extensions only.
    for (const char* suffix : { "OES", "EXT", "ARB", "APPLE" }) {
        std::string alt = std::string(name) + suffix;
        if (loadProc(out, alt.c_str()))
            return true;
    }
    return false;
}

const GLFunctions* loadGLFunctions() {
    void* context = currentNativeGLContext();
    if (context && context == s_functionsContext)
        return s_functionsValid ? &s_functions : nullptr;

    s_functions = GLFunctions();
    s_functionsContext = context;
    s_functionsValid = true;

#define BISMUTH_GL_LOAD(Ret, Name, ...) \
    if (!loadProcWithSuffixes(s_functions.Name, #Name) && std::strcmp(#Name, "glGetStringi") != 0) { \
        logf(LogLevel::Warn, "Bismuth: missing GL function %s", #Name); \
        s_functionsValid = false; \
    }
    BISMUTH_GL_FUNCTIONS(BISMUTH_GL_LOAD)
#undef BISMUTH_GL_LOAD

#define BISMUTH_GL_LOAD_OPTIONAL(Ret, Name, ...) loadProcWithSuffixes(s_functions.Name, #Name);
    BISMUTH_GL_OPTIONAL_FUNCTIONS(BISMUTH_GL_LOAD_OPTIONAL)
#undef BISMUTH_GL_LOAD_OPTIONAL

    return s_functionsValid ? &s_functions : nullptr;
}

const GLFunctions& glf() {
    return s_functions;
}

//// CONTEXT INFO ////

bool HostGLInfo::hasExtension(const char* name) const {
    std::string needle = std::string(" ") + name + " ";
    return extensions.find(needle) != std::string::npos;
}

static std::string glString(GLenum name) {
    auto str = s_functions.glGetString(name);
    return str ? reinterpret_cast<const char*>(str) : "";
}

bool queryHostGL(HostGLInfo& info) {
    info = HostGLInfo();

    if (!loadGLFunctions())
        return false;
    auto& gl = s_functions;

    info.nativeContext = currentNativeGLContext();
    info.nativeWindow = currentNativeWindow();
#if defined(__linux__) && defined(BISMUTH_HOST_GLX)
    info.nativeDisplay = currentGLXDisplay();
#endif
    if (!info.nativeContext)
        return false;

    info.vendor   = glString(gl::VENDOR);
    info.renderer = glString(gl::RENDERER);
    info.version  = glString(gl::VERSION);

    const char* version = info.version.c_str();
    if (std::strncmp(version, "OpenGL ES", 9) == 0) {
        info.isES = true;
        version += 9;
    }
    while (*version && (*version < '0' || *version > '9'))
        version++;
    info.major = std::atoi(version);
    const char* dot = std::strchr(version, '.');
    info.minor = dot ? std::atoi(dot + 1) : 0;

    info.isANGLE = info.renderer.find("ANGLE") != std::string::npos
                || info.version.find("ANGLE") != std::string::npos;

    info.extensions = " ";
    if (info.major >= 3 && gl.glGetStringi) {
        GLint count = 0;
        gl.glGetIntegerv(gl::NUM_EXTENSIONS, &count);
        for (GLint i = 0; i < count; i++) {
            auto ext = gl.glGetStringi(gl::EXTENSIONS, (GLuint)i);
            if (ext) {
                info.extensions += reinterpret_cast<const char*>(ext);
                info.extensions += ' ';
            }
        }
    } else {
        info.extensions += glString(gl::EXTENSIONS);
        info.extensions += ' ';
    }
    while (gl.glGetError() != gl::NO_ERROR_) {}

    const bool es3 = info.isES && info.major >= 3;
    const bool gl3 = !info.isES && info.major >= 3;

    info.hasVAO = gl.glBindVertexArray && (es3 || gl3
        || info.hasExtension("GL_ARB_vertex_array_object")
        || info.hasExtension("GL_OES_vertex_array_object")
        || info.hasExtension("GL_APPLE_vertex_array_object"));

    info.hasSamplerObjects = gl.glBindSampler && (es3 || info.atLeast(3, 3) || info.hasExtension("GL_ARB_sampler_objects"));
    info.hasSync = gl.glFenceSync && gl.glClientWaitSync && (es3 || info.atLeast(3, 2) || info.hasExtension("GL_ARB_sync"));

    info.hasComputeShaders = info.isES ? info.atLeast(3, 1) : (info.atLeast(4, 3) || info.hasExtension("GL_ARB_compute_shader"));
    info.hasMultiDrawIndirect = info.isES ? info.hasExtension("GL_EXT_multi_draw_indirect")
                                          : (info.atLeast(4, 3) || info.hasExtension("GL_ARB_multi_draw_indirect"));
    if (info.hasComputeShaders) {
        GLint blocks = 0;
        gl.glGetIntegerv(0x90D6 /* GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS */, &blocks);
        while (gl.glGetError() != gl::NO_ERROR_) {}
        info.maxVertexStorageBlocks = blocks;
    }

    const bool memoryObject = info.hasExtension("GL_EXT_memory_object")
        && gl.glCreateMemoryObjectsEXT && gl.glTexStorageMem2DEXT;
    info.hasMemoryObjectFd    = memoryObject && info.hasExtension("GL_EXT_memory_object_fd") && gl.glImportMemoryFdEXT;
    info.hasMemoryObjectWin32 = memoryObject && info.hasExtension("GL_EXT_memory_object_win32") && gl.glImportMemoryWin32HandleEXT;

    const bool semaphore = info.hasExtension("GL_EXT_semaphore")
        && gl.glGenSemaphoresEXT && gl.glWaitSemaphoreEXT && gl.glSignalSemaphoreEXT;
    info.hasSemaphoreFd    = semaphore && info.hasExtension("GL_EXT_semaphore_fd") && gl.glImportSemaphoreFdEXT;
    info.hasSemaphoreWin32 = semaphore && info.hasExtension("GL_EXT_semaphore_win32") && gl.glImportSemaphoreWin32HandleEXT;

    info.hasEGLImage = gl.glEGLImageTargetTexture2DOES && info.hasExtension("GL_OES_EGL_image");

    if (memoryObject || semaphore) {
        if (gl.glGetUnsignedBytei_vEXT) {
            gl.glGetUnsignedBytei_vEXT(gl::DEVICE_UUID_EXT, 0, info.deviceUuid);
            info.hasDeviceUuid = gl.glGetError() == gl::NO_ERROR_;
        }
        while (gl.glGetError() != gl::NO_ERROR_) {}
    }

    return true;
}

//// STATE GUARD ////

static constexpr u32 kSavedAttribs = 8;

GLStateGuard::GLStateGuard(const HostGLInfo& info) : info(info) {
    auto& gl = s_functions;
    auto& s = state;

    gl.glGetIntegerv(gl::CURRENT_PROGRAM, &s.program);
    if (info.hasVAO)
        gl.glGetIntegerv(gl::VERTEX_ARRAY_BINDING, &s.vao);
    gl.glGetIntegerv(gl::ARRAY_BUFFER_BINDING, &s.arrayBuffer);
    gl.glGetIntegerv(gl::ELEMENT_ARRAY_BUFFER_BINDING, &s.elementArrayBuffer);

    gl.glGetIntegerv(gl::ACTIVE_TEXTURE, &s.activeTexture);
    for (GLuint i = 0; i < 2; i++) {
        gl.glActiveTexture(gl::TEXTURE0 + i);
        gl.glGetIntegerv(gl::TEXTURE_BINDING_2D, &s.texture2D[i]);
    }
    gl.glActiveTexture((GLenum)s.activeTexture);

    gl.glGetIntegerv(gl::FRAMEBUFFER_BINDING, &s.drawFramebuffer);
    s.readFramebuffer = s.drawFramebuffer;
    if (info.major >= 3)
        gl.glGetIntegerv(gl::READ_FRAMEBUFFER_BINDING, &s.readFramebuffer);

    gl.glGetIntegerv(gl::VIEWPORT, s.viewport);
    gl.glGetIntegerv(gl::SCISSOR_BOX, s.scissorBox);
    s.scissorTest = gl.glIsEnabled(gl::SCISSOR_TEST);

    s.blend = gl.glIsEnabled(gl::BLEND);
    gl.glGetIntegerv(gl::BLEND_SRC_RGB, &s.blendSrcRGB);
    gl.glGetIntegerv(gl::BLEND_DST_RGB, &s.blendDstRGB);
    gl.glGetIntegerv(gl::BLEND_SRC_ALPHA, &s.blendSrcAlpha);
    gl.glGetIntegerv(gl::BLEND_DST_ALPHA, &s.blendDstAlpha);
    gl.glGetIntegerv(gl::BLEND_EQUATION_RGB, &s.blendEqRGB);
    gl.glGetIntegerv(gl::BLEND_EQUATION_ALPHA, &s.blendEqAlpha);
    gl.glGetFloatv(gl::BLEND_COLOR, s.blendColor);

    s.depthTest = gl.glIsEnabled(gl::DEPTH_TEST);
    gl.glGetBooleanv(gl::DEPTH_WRITEMASK, &s.depthMask);
    gl.glGetIntegerv(gl::DEPTH_FUNC, &s.depthFunc);
    s.stencilTest = gl.glIsEnabled(gl::STENCIL_TEST);
    s.cullFace = gl.glIsEnabled(gl::CULL_FACE);
    gl.glGetIntegerv(gl::FRONT_FACE, &s.frontFace);
    gl.glGetBooleanv(gl::COLOR_WRITEMASK, s.colorMask);
    gl.glGetIntegerv(gl::UNPACK_ALIGNMENT, &s.unpackAlignment);
    gl.glGetIntegerv(gl::PACK_ALIGNMENT, &s.packAlignment);

    s.enabledAttribs = 0;
    for (GLuint i = 0; i < kSavedAttribs; i++) {
        GLint enabled = 0;
        gl.glGetVertexAttribiv(i, gl::VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
        if (enabled)
            s.enabledAttribs |= 1u << i;
    }
}

static void setEnabled(GLenum cap, GLboolean enabled) {
    if (enabled) s_functions.glEnable(cap);
    else         s_functions.glDisable(cap);
}

GLStateGuard::~GLStateGuard() {
    auto& gl = s_functions;
    const auto& s = state;

    gl.glUseProgram((GLuint)s.program);

    if (info.hasVAO)
        gl.glBindVertexArray((GLuint)s.vao);
    gl.glBindBuffer(gl::ARRAY_BUFFER, (GLuint)s.arrayBuffer);
    gl.glBindBuffer(gl::ELEMENT_ARRAY_BUFFER, (GLuint)s.elementArrayBuffer);

    for (GLuint i = 0; i < kSavedAttribs; i++) {
        if (s.enabledAttribs & (1u << i)) gl.glEnableVertexAttribArray(i);
        else                              gl.glDisableVertexAttribArray(i);
    }

    for (GLuint i = 0; i < 2; i++) {
        gl.glActiveTexture(gl::TEXTURE0 + i);
        gl.glBindTexture(gl::TEXTURE_2D, (GLuint)s.texture2D[i]);
        // cocos2d never uses sampler objects; bgfx binds them per texture unit.
        if (info.hasSamplerObjects)
            gl.glBindSampler(i, 0);
    }
    gl.glActiveTexture((GLenum)s.activeTexture);

    gl.glBindFramebuffer(gl::FRAMEBUFFER, (GLuint)s.drawFramebuffer);
    if (info.major >= 3 && s.readFramebuffer != s.drawFramebuffer)
        gl.glBindFramebuffer(gl::READ_FRAMEBUFFER, (GLuint)s.readFramebuffer);

    gl.glViewport(s.viewport[0], s.viewport[1], s.viewport[2], s.viewport[3]);
    gl.glScissor(s.scissorBox[0], s.scissorBox[1], s.scissorBox[2], s.scissorBox[3]);
    setEnabled(gl::SCISSOR_TEST, s.scissorTest);

    setEnabled(gl::BLEND, s.blend);
    gl.glBlendFuncSeparate((GLenum)s.blendSrcRGB, (GLenum)s.blendDstRGB, (GLenum)s.blendSrcAlpha, (GLenum)s.blendDstAlpha);
    gl.glBlendEquationSeparate((GLenum)s.blendEqRGB, (GLenum)s.blendEqAlpha);
    gl.glBlendColor(s.blendColor[0], s.blendColor[1], s.blendColor[2], s.blendColor[3]);

    setEnabled(gl::DEPTH_TEST, s.depthTest);
    gl.glDepthMask(s.depthMask);
    gl.glDepthFunc((GLenum)s.depthFunc);
    setEnabled(gl::STENCIL_TEST, s.stencilTest);
    setEnabled(gl::CULL_FACE, s.cullFace);
    gl.glFrontFace((GLenum)s.frontFace);
    gl.glColorMask(s.colorMask[0], s.colorMask[1], s.colorMask[2], s.colorMask[3]);
    gl.glPixelStorei(gl::UNPACK_ALIGNMENT, s.unpackAlignment);
    gl.glPixelStorei(gl::PACK_ALIGNMENT, s.packAlignment);

    // Drop any error raised by bgfx (or the restore) so the host's own error
    // checks stay clean. Done once: each glGetError can stall the driver.
    while (gl.glGetError() != gl::NO_ERROR_) {}
}

//// RUNTIME GLSL ////

static GLuint compileStage(GLenum type, const std::string& source) {
    auto& gl = s_functions;
    GLuint shader = gl.glCreateShader(type);
    const char* src = source.c_str();
    gl.glShaderSource(shader, 1, &src, nullptr);
    gl.glCompileShader(shader);

    GLint ok = 0;
    gl.glGetShaderiv(shader, gl::COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        gl.glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
        logf(LogLevel::Error, "Bismuth: GL shader compile failed: %s", log);
        gl.glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint compileHostProgram(
    const HostGLInfo& info,
    const char* vertexSource,
    const char* fragmentSource,
    const char* const* attributeNames,
    u32 attributeCount,
    const char* fragmentPrologue
) {
    auto& gl = s_functions;

    std::string prologue = info.isES
        ? "#version 100\nprecision highp float;\n"
        : "#version 120\n";

    GLuint vs = compileStage(gl::VERTEX_SHADER, prologue + vertexSource);
    GLuint fs = compileStage(gl::FRAGMENT_SHADER,
        (info.isES ? std::string("#version 100\n") + fragmentPrologue + "\nprecision mediump float;\n"
                   : std::string("#version 120\n") + fragmentPrologue + "\n")
        + fragmentSource);

    if (!vs || !fs) {
        if (vs) gl.glDeleteShader(vs);
        if (fs) gl.glDeleteShader(fs);
        return 0;
    }

    GLuint program = gl.glCreateProgram();
    gl.glAttachShader(program, vs);
    gl.glAttachShader(program, fs);
    for (u32 i = 0; i < attributeCount; i++)
        gl.glBindAttribLocation(program, i, attributeNames[i]);
    gl.glLinkProgram(program);
    gl.glDeleteShader(vs);
    gl.glDeleteShader(fs);

    GLint ok = 0;
    gl.glGetProgramiv(program, gl::LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        gl.glGetProgramInfoLog(program, sizeof(log) - 1, nullptr, log);
        logf(LogLevel::Error, "Bismuth: GL program link failed: %s", log);
        gl.glDeleteProgram(program);
        return 0;
    }
    return program;
}

} // namespace bismuth::gfx
