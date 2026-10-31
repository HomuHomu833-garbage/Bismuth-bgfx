#pragma once

/*
    Access to the host application's (Geometry Dash's) OpenGL context without
    depending on cocos2d's GL headers: a minimal function loader, information
    about the context, and an exact save/restore of the state bgfx touches.

    Restoring the *exact* previous state matters: cocos2d caches GL state
    (bound program, textures, blend function, enabled attributes), so leaving
    anything different would desynchronize it.
*/

#include <core/Types.hpp>
#include <string>

#if defined(_WIN32)
#   define BISMUTH_GL_APIENTRY __stdcall
#else
#   define BISMUTH_GL_APIENTRY
#endif

namespace bismuth::gfx {

using GLenum     = unsigned int;
using GLuint     = unsigned int;
using GLint      = int;
using GLsizei    = int;
using GLboolean  = unsigned char;
using GLbitfield = unsigned int;
using GLfloat    = float;
using GLuint64   = uint64_t;
using GLsizeiptr = intptr_t;
using GLchar     = char;
using GLubyte    = unsigned char;
struct __GLsync;
using GLsync     = __GLsync*;

namespace gl {
    constexpr GLenum NO_ERROR_                 = 0;
    constexpr GLenum TEXTURE_2D                = 0x0DE1;
    constexpr GLenum TEXTURE_RECTANGLE         = 0x84F5;
    constexpr GLenum TEXTURE0                  = 0x84C0;
    constexpr GLenum ACTIVE_TEXTURE            = 0x84E0;
    constexpr GLenum TEXTURE_BINDING_2D        = 0x8069;
    constexpr GLenum TEXTURE_BINDING_RECTANGLE = 0x84F6;
    constexpr GLenum SAMPLER_BINDING           = 0x8919;
    constexpr GLenum CURRENT_PROGRAM           = 0x8B8D;
    constexpr GLenum VERTEX_ARRAY_BINDING      = 0x85B5;
    constexpr GLenum ARRAY_BUFFER              = 0x8892;
    constexpr GLenum ELEMENT_ARRAY_BUFFER      = 0x8893;
    constexpr GLenum ARRAY_BUFFER_BINDING      = 0x8894;
    constexpr GLenum ELEMENT_ARRAY_BUFFER_BINDING = 0x8895;
    constexpr GLenum FRAMEBUFFER               = 0x8D40;
    constexpr GLenum READ_FRAMEBUFFER          = 0x8CA8;
    constexpr GLenum DRAW_FRAMEBUFFER          = 0x8CA9;
    constexpr GLenum FRAMEBUFFER_BINDING       = 0x8CA6;
    constexpr GLenum READ_FRAMEBUFFER_BINDING  = 0x8CAA;
    constexpr GLenum RENDERBUFFER              = 0x8D41;
    constexpr GLenum COLOR_ATTACHMENT0         = 0x8CE0;
    constexpr GLenum FRAMEBUFFER_COMPLETE      = 0x8CD5;
    constexpr GLenum VIEWPORT                  = 0x0BA2;
    constexpr GLenum SCISSOR_BOX               = 0x0C10;
    constexpr GLenum SCISSOR_TEST              = 0x0C11;
    constexpr GLenum BLEND                     = 0x0BE2;
    constexpr GLenum BLEND_SRC_RGB             = 0x80C9;
    constexpr GLenum BLEND_DST_RGB             = 0x80C8;
    constexpr GLenum BLEND_SRC_ALPHA           = 0x80CB;
    constexpr GLenum BLEND_DST_ALPHA           = 0x80CA;
    constexpr GLenum BLEND_EQUATION_RGB        = 0x8009;
    constexpr GLenum BLEND_EQUATION_ALPHA      = 0x883D;
    constexpr GLenum BLEND_COLOR               = 0x8005;
    constexpr GLenum FUNC_ADD                  = 0x8006;
    constexpr GLenum ONE                       = 1;
    constexpr GLenum ONE_MINUS_SRC_ALPHA       = 0x0303;
    constexpr GLenum DEPTH_TEST                = 0x0B71;
    constexpr GLenum DEPTH_WRITEMASK           = 0x0B72;
    constexpr GLenum DEPTH_FUNC                = 0x0B74;
    constexpr GLenum STENCIL_TEST              = 0x0B90;
    constexpr GLenum CULL_FACE                 = 0x0B44;
    constexpr GLenum FRONT_FACE                = 0x0B46;
    constexpr GLenum COLOR_WRITEMASK           = 0x0C23;
    constexpr GLenum UNPACK_ALIGNMENT          = 0x0CF5;
    constexpr GLenum PACK_ALIGNMENT            = 0x0D05;
    constexpr GLenum VERTEX_ATTRIB_ARRAY_ENABLED = 0x8622;
    constexpr GLenum VENDOR                    = 0x1F00;
    constexpr GLenum RENDERER                  = 0x1F01;
    constexpr GLenum VERSION                   = 0x1F02;
    constexpr GLenum EXTENSIONS                = 0x1F03;
    constexpr GLenum NUM_EXTENSIONS            = 0x821D;
    constexpr GLenum MAJOR_VERSION             = 0x821B;
    constexpr GLenum MINOR_VERSION             = 0x821C;
    constexpr GLenum TRIANGLES                 = 0x0004;
    constexpr GLenum TRIANGLE_STRIP            = 0x0005;
    constexpr GLenum FLOAT                     = 0x1406;
    constexpr GLenum UNSIGNED_BYTE             = 0x1401;
    constexpr GLenum RGBA                      = 0x1908;
    constexpr GLenum RGBA8                     = 0x8058;
    constexpr GLenum BGRA                      = 0x80E1;
    constexpr GLenum UNSIGNED_INT_8_8_8_8_REV  = 0x8367;
    constexpr GLenum TEXTURE_MIN_FILTER        = 0x2801;
    constexpr GLenum TEXTURE_MAG_FILTER        = 0x2800;
    constexpr GLenum TEXTURE_WRAP_S            = 0x2802;
    constexpr GLenum TEXTURE_WRAP_T            = 0x2803;
    constexpr GLenum NEAREST                   = 0x2600;
    constexpr GLenum LINEAR                    = 0x2601;
    constexpr GLenum CLAMP_TO_EDGE             = 0x812F;
    constexpr GLenum COMPILE_STATUS            = 0x8B81;
    constexpr GLenum LINK_STATUS               = 0x8B82;
    constexpr GLenum VERTEX_SHADER             = 0x8B31;
    constexpr GLenum FRAGMENT_SHADER           = 0x8B30;
    constexpr GLenum STATIC_DRAW               = 0x88E4;
    constexpr GLenum COLOR_BUFFER_BIT          = 0x4000;
    constexpr GLenum SYNC_GPU_COMMANDS_COMPLETE = 0x9117;
    constexpr GLenum SYNC_FLUSH_COMMANDS_BIT   = 0x00000001;
    constexpr GLenum TIMEOUT_EXPIRED           = 0x911B;
    constexpr GLenum WAIT_FAILED               = 0x911D;
    // EXT_memory_object / EXT_semaphore
    constexpr GLenum TEXTURE_TILING_EXT        = 0x9580;
    constexpr GLenum DEDICATED_MEMORY_OBJECT_EXT = 0x9581;
    constexpr GLenum OPTIMAL_TILING_EXT        = 0x9584;
    constexpr GLenum HANDLE_TYPE_OPAQUE_FD_EXT = 0x9586;
    constexpr GLenum HANDLE_TYPE_OPAQUE_WIN32_EXT = 0x9587;
    constexpr GLenum NUM_DEVICE_UUIDS_EXT      = 0x9596;
    constexpr GLenum DEVICE_UUID_EXT           = 0x9597;
    constexpr GLenum LAYOUT_SHADER_READ_ONLY_EXT = 0x9591;
    constexpr GLenum LAYOUT_COLOR_ATTACHMENT_EXT = 0x958E;
}

/// GL entry points used by Bismuth itself (bgfx loads its own).
struct GLFunctions {
#define BISMUTH_GL_FUNCTIONS(F) \
    F(const GLubyte*, glGetString, GLenum) \
    F(const GLubyte*, glGetStringi, GLenum, GLuint) \
    F(GLenum, glGetError) \
    F(void, glGetIntegerv, GLenum, GLint*) \
    F(void, glGetFloatv, GLenum, GLfloat*) \
    F(void, glGetBooleanv, GLenum, GLboolean*) \
    F(GLboolean, glIsEnabled, GLenum) \
    F(void, glEnable, GLenum) \
    F(void, glDisable, GLenum) \
    F(void, glViewport, GLint, GLint, GLsizei, GLsizei) \
    F(void, glScissor, GLint, GLint, GLsizei, GLsizei) \
    F(void, glBlendFuncSeparate, GLenum, GLenum, GLenum, GLenum) \
    F(void, glBlendEquationSeparate, GLenum, GLenum) \
    F(void, glBlendColor, GLfloat, GLfloat, GLfloat, GLfloat) \
    F(void, glDepthMask, GLboolean) \
    F(void, glDepthFunc, GLenum) \
    F(void, glFrontFace, GLenum) \
    F(void, glColorMask, GLboolean, GLboolean, GLboolean, GLboolean) \
    F(void, glPixelStorei, GLenum, GLint) \
    F(void, glUseProgram, GLuint) \
    F(void, glActiveTexture, GLenum) \
    F(void, glBindTexture, GLenum, GLuint) \
    F(void, glGenTextures, GLsizei, GLuint*) \
    F(void, glDeleteTextures, GLsizei, const GLuint*) \
    F(void, glTexParameteri, GLenum, GLenum, GLint) \
    F(void, glTexImage2D, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) \
    F(void, glBindBuffer, GLenum, GLuint) \
    F(void, glGenBuffers, GLsizei, GLuint*) \
    F(void, glDeleteBuffers, GLsizei, const GLuint*) \
    F(void, glBufferData, GLenum, GLsizeiptr, const void*, GLenum) \
    F(void, glBindFramebuffer, GLenum, GLuint) \
    F(void, glGenFramebuffers, GLsizei, GLuint*) \
    F(void, glDeleteFramebuffers, GLsizei, const GLuint*) \
    F(void, glFramebufferTexture2D, GLenum, GLenum, GLenum, GLuint, GLint) \
    F(GLenum, glCheckFramebufferStatus, GLenum) \
    F(void, glReadPixels, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) \
    F(void, glClearColor, GLfloat, GLfloat, GLfloat, GLfloat) \
    F(void, glClear, GLbitfield) \
    F(void, glGetVertexAttribiv, GLuint, GLenum, GLint*) \
    F(void, glEnableVertexAttribArray, GLuint) \
    F(void, glDisableVertexAttribArray, GLuint) \
    F(void, glVertexAttribPointer, GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) \
    F(void, glDrawArrays, GLenum, GLint, GLsizei) \
    F(GLuint, glCreateShader, GLenum) \
    F(void, glDeleteShader, GLuint) \
    F(void, glShaderSource, GLuint, GLsizei, const GLchar* const*, const GLint*) \
    F(void, glCompileShader, GLuint) \
    F(void, glGetShaderiv, GLuint, GLenum, GLint*) \
    F(void, glGetShaderInfoLog, GLuint, GLsizei, GLsizei*, GLchar*) \
    F(GLuint, glCreateProgram) \
    F(void, glDeleteProgram, GLuint) \
    F(void, glAttachShader, GLuint, GLuint) \
    F(void, glBindAttribLocation, GLuint, GLuint, const GLchar*) \
    F(void, glLinkProgram, GLuint) \
    F(void, glGetProgramiv, GLuint, GLenum, GLint*) \
    F(void, glGetProgramInfoLog, GLuint, GLsizei, GLsizei*, GLchar*) \
    F(GLint, glGetUniformLocation, GLuint, const GLchar*) \
    F(void, glUniform1i, GLint, GLint) \
    F(void, glUniform1f, GLint, GLfloat) \
    F(void, glUniform2f, GLint, GLfloat, GLfloat) \
    F(void, glFlush) \
    F(void, glFinish)

#define BISMUTH_GL_OPTIONAL_FUNCTIONS(F) \
    F(void, glBindVertexArray, GLuint) \
    F(void, glBindSampler, GLuint, GLuint) \
    F(GLsync, glFenceSync, GLenum, GLbitfield) \
    F(GLenum, glClientWaitSync, GLsync, GLbitfield, GLuint64) \
    F(void, glDeleteSync, GLsync) \
    F(void, glGetUnsignedBytevEXT, GLenum, GLubyte*) \
    F(void, glGetUnsignedBytei_vEXT, GLenum, GLuint, GLubyte*) \
    F(void, glCreateMemoryObjectsEXT, GLsizei, GLuint*) \
    F(void, glDeleteMemoryObjectsEXT, GLsizei, const GLuint*) \
    F(void, glMemoryObjectParameterivEXT, GLuint, GLenum, const GLint*) \
    F(void, glImportMemoryFdEXT, GLuint, GLuint64, GLenum, GLint) \
    F(void, glImportMemoryWin32HandleEXT, GLuint, GLuint64, GLenum, void*) \
    F(void, glTexStorageMem2DEXT, GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLuint, GLuint64) \
    F(void, glGenSemaphoresEXT, GLsizei, GLuint*) \
    F(void, glDeleteSemaphoresEXT, GLsizei, const GLuint*) \
    F(void, glImportSemaphoreFdEXT, GLuint, GLenum, GLint) \
    F(void, glImportSemaphoreWin32HandleEXT, GLuint, GLenum, void*) \
    F(void, glWaitSemaphoreEXT, GLuint, GLuint, const GLuint*, GLuint, const GLuint*, const GLenum*) \
    F(void, glSignalSemaphoreEXT, GLuint, GLuint, const GLuint*, GLuint, const GLuint*, const GLenum*) \
    F(void, glEGLImageTargetTexture2DOES, GLenum, void*)

#define BISMUTH_GL_DECLARE(Ret, Name, ...) Ret (BISMUTH_GL_APIENTRY *Name)(__VA_ARGS__) = nullptr;
    BISMUTH_GL_FUNCTIONS(BISMUTH_GL_DECLARE)
    BISMUTH_GL_OPTIONAL_FUNCTIONS(BISMUTH_GL_DECLARE)
#undef BISMUTH_GL_DECLARE
};

/// Loaded once per context. Returns nullptr if a required function is missing.
const GLFunctions* loadGLFunctions();
const GLFunctions& glf();

/// Looks up any GL / platform GL function (wglGetProcAddress, eglGetProcAddress, dlsym).
void* getGLProcAddress(const char* name);

struct HostGLInfo {
    void* nativeContext = nullptr; ///< HGLRC / EGLContext / NSOpenGLContext* / EAGLContext* / GLXContext
    void* nativeWindow = nullptr;  ///< HWND on Windows (needed by bgfx's WGL code), otherwise null
    void* nativeDisplay = nullptr; ///< X11 Display* for GLX (tests only)

    bool isES = false;
    bool isANGLE = false;
    int  major = 0;
    int  minor = 0;
    std::string vendor;
    std::string renderer;
    std::string version;

    bool hasVAO = false;
    bool hasSamplerObjects = false;
    bool hasSync = false;
    bool hasMemoryObjectFd = false;
    bool hasMemoryObjectWin32 = false;
    bool hasSemaphoreFd = false;
    bool hasSemaphoreWin32 = false;
    bool hasEGLImage = false;
    bool hasComputeShaders = false;
    bool hasMultiDrawIndirect = false;
    int  maxVertexStorageBlocks = 0;

    bool hasDeviceUuid = false;
    u8   deviceUuid[16] = {};

    bool hasExtension(const char* name) const;
    bool atLeast(int maj, int min) const { return major > maj || (major == maj && minor >= min); }

    std::string extensions; ///< space separated, with leading and trailing space
};

/// Queries the context that is current on the calling thread.
bool queryHostGL(HostGLInfo& info);

/// The native handle of the GL context current on this thread (for loss detection).
void* currentNativeGLContext();

/// GL state that bgfx (or Bismuth's own GL drawing) may modify.
struct GLStateSnapshot {
    GLint program = 0;
    GLint vao = 0;
    GLint arrayBuffer = 0;
    GLint elementArrayBuffer = 0;
    GLint activeTexture = 0;
    GLint texture2D[2] = {};
    GLint drawFramebuffer = 0;
    GLint readFramebuffer = 0;
    GLint viewport[4] = {};
    GLint scissorBox[4] = {};
    GLboolean scissorTest = 0;
    GLboolean blend = 0;
    GLint blendSrcRGB = 0, blendDstRGB = 0, blendSrcAlpha = 0, blendDstAlpha = 0;
    GLint blendEqRGB = 0, blendEqAlpha = 0;
    GLfloat blendColor[4] = {};
    GLboolean depthTest = 0;
    GLboolean depthMask = 0;
    GLint depthFunc = 0;
    GLboolean stencilTest = 0;
    GLboolean cullFace = 0;
    GLint frontFace = 0;
    GLboolean colorMask[4] = {};
    GLint unpackAlignment = 4;
    GLint packAlignment = 4;
    u32 enabledAttribs = 0;
};

class GLStateGuard {
public:
    /// Records the current state.
    explicit GLStateGuard(const HostGLInfo& info);
    /// Restores it.
    ~GLStateGuard();

    const GLStateSnapshot& snapshot() const { return state; }

    GLStateGuard(const GLStateGuard&) = delete;
    GLStateGuard& operator=(const GLStateGuard&) = delete;

private:
    const HostGLInfo& info;
    GLStateSnapshot state;
};

/// Compiles a small GLSL program at runtime, adding the right #version /
/// precision prologue for the context (GLSL 1.20 or ESSL 1.00).
GLuint compileHostProgram(
    const HostGLInfo& info,
    const char* vertexSource,
    const char* fragmentSource,
    const char* const* attributeNames,
    u32 attributeCount,
    const char* fragmentPrologue = ""
);

} // namespace bismuth::gfx
