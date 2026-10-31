/*
    Vulkan <-> host OpenGL sharing.

    Memory:
      - Windows / Linux: Vulkan exports opaque memory (Win32 handle / fd), GL imports
        it with GL_EXT_memory_object(_win32/_fd) and glTexStorageMem2DEXT.
      - Android: an AHardwareBuffer is imported by both Vulkan
        (VK_ANDROID_external_memory_android_hardware_buffer) and GL (EGLImage).

    Synchronization (GPU side, no CPU stalls) when available:
      - Windows / Linux: exportable binary semaphores + GL_EXT_semaphore(_win32/_fd).
      - Android: sync fds (VK_KHR_external_semaphore_fd SYNC_FD) + EGL native fences.
    Otherwise falls back to waiting on the CPU (vkQueueWaitIdle / glClientWaitSync).
*/

#include "Interop.hpp"
#include <bgfx/platform.h>

#if defined(_WIN32)
#   define WIN32_LEAN_AND_MEAN
#   define NOMINMAX
#   include <windows.h>
#   define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__ANDROID__)
#   include <android/hardware_buffer.h>
#   include <EGL/egl.h>
#   include <EGL/eglext.h>
#   include <dlfcn.h>
#   include <unistd.h>
#   define VK_USE_PLATFORM_ANDROID_KHR
#elif !defined(__APPLE__)
#   include <unistd.h>
#endif

#if !defined(__APPLE__)

#define VK_NO_PROTOTYPES
#include <vulkan-local/vulkan.h>

#include <cstring>
#include <vector>

namespace bismuth::gfx {

class InteropVulkan;

namespace {

enum class MemoryMode { OpaqueWin32, OpaqueFd, AndroidHardwareBuffer };
enum class SyncMode { CpuWait, OpaqueSemaphores, AndroidFences };

struct VkFns {
    PFN_vkGetDeviceProcAddr getDeviceProcAddr = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties getPhysicalDeviceMemoryProperties = nullptr;
    PFN_vkGetPhysicalDeviceProperties getPhysicalDeviceProperties = nullptr;
    PFN_vkGetImageMemoryRequirements getImageMemoryRequirements = nullptr;
    PFN_vkAllocateMemory allocateMemory = nullptr;
    PFN_vkFreeMemory freeMemory = nullptr;
    PFN_vkBindImageMemory bindImageMemory = nullptr;
    PFN_vkQueueWaitIdle queueWaitIdle = nullptr;
    PFN_vkCreateSemaphore createSemaphore = nullptr;
    PFN_vkDestroySemaphore destroySemaphore = nullptr;
#if defined(_WIN32)
    PFN_vkGetMemoryWin32HandleKHR getMemoryWin32Handle = nullptr;
    PFN_vkGetSemaphoreWin32HandleKHR getSemaphoreWin32Handle = nullptr;
#else
    PFN_vkGetMemoryFdKHR getMemoryFd = nullptr;
    PFN_vkGetSemaphoreFdKHR getSemaphoreFd = nullptr;
    PFN_vkImportSemaphoreFdKHR importSemaphoreFd = nullptr;
#endif
#if defined(__ANDROID__)
    PFN_vkGetAndroidHardwareBufferPropertiesANDROID getAndroidHardwareBufferProperties = nullptr;
#endif
};

template <typename T>
uint64_t handleBits(T handle) {
    uint64_t bits = 0;
    std::memcpy(&bits, &handle, sizeof(handle));
    return bits;
}

template <typename T>
T fromBits(uint64_t bits) {
    T handle {};
    std::memcpy(&handle, &bits, sizeof(handle));
    return handle;
}

#if defined(__ANDROID__)
// AHardwareBuffer functions are API level 26+; GD supports older devices, so load at runtime.
struct AhbFns {
    int  (*allocate)(const AHardwareBuffer_Desc*, AHardwareBuffer**) = nullptr;
    void (*release)(AHardwareBuffer*) = nullptr;
    PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC getNativeClientBuffer = nullptr;
    PFNEGLCREATEIMAGEKHRPROC  createImage = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC destroyImage = nullptr;
    PFNEGLCREATESYNCKHRPROC   createSync = nullptr;
    PFNEGLDESTROYSYNCKHRPROC  destroySync = nullptr;
    PFNEGLWAITSYNCKHRPROC     waitSync = nullptr;
    PFNEGLDUPNATIVEFENCEFDANDROIDPROC dupNativeFenceFd = nullptr;

    bool load() {
        void* lib = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
        if (!lib) return false;
        allocate = (decltype(allocate))dlsym(lib, "AHardwareBuffer_allocate");
        release  = (decltype(release))dlsym(lib, "AHardwareBuffer_release");
        getNativeClientBuffer = (PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC)eglGetProcAddress("eglGetNativeClientBufferANDROID");
        createImage  = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
        destroyImage = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
        createSync   = (PFNEGLCREATESYNCKHRPROC)eglGetProcAddress("eglCreateSyncKHR");
        destroySync  = (PFNEGLDESTROYSYNCKHRPROC)eglGetProcAddress("eglDestroySyncKHR");
        waitSync     = (PFNEGLWAITSYNCKHRPROC)eglGetProcAddress("eglWaitSyncKHR");
        dupNativeFenceFd = (PFNEGLDUPNATIVEFENCEFDANDROIDPROC)eglGetProcAddress("eglDupNativeFenceFDANDROID");
        return allocate && release && getNativeClientBuffer && createImage && destroyImage;
    }

    bool hasFences() const { return createSync && destroySync && waitSync && dupNativeFenceFd; }
};
#endif

struct TargetData {
    InteropVulkan* owner = nullptr;
    SharedTarget* target = nullptr;
    VkExternalMemoryImageCreateInfo externalInfo {};
    bgfx::VkExternalTextureHooks hooks {};
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    bool allocated = false;
#if defined(_WIN32)
    HANDLE memoryHandle = nullptr;
#else
    int memoryFd = -1;
#endif
    GLuint glMemoryObject = 0;
#if defined(__ANDROID__)
    AHardwareBuffer* hardwareBuffer = nullptr;
    EGLImageKHR eglImage = EGL_NO_IMAGE_KHR;
#endif
};

} // namespace

class InteropVulkan final : public Interop {
public:
    bool init(const HostGLInfo& host, std::string& error);
    ~InteropVulkan() override;

    bool createTarget(SharedTarget& target, u32 width, u32 height) override;
    void destroyTarget(SharedTarget& target) override;

    void beforeNativeFrame() override;
    void afterNativeFrame() override;
    void beforeHostRead() override;
    void afterHostRead() override;

    const char* describe() const override { return description.c_str(); }

    static int32_t allocateAndBind(void* userData, void* device, uint64_t image, void* outMemory);

private:
    int32_t allocateTargetMemory(TargetData& data, VkImage image, VkDeviceMemory* outMemory);
    bool importIntoGL(TargetData& data);
    bool createSemaphores();
    void hostWait();
    void hostSignal();
    u32 collectTextures(GLuint* textures, GLenum* layouts);

    const HostGLInfo* host = nullptr;
    bgfx::VkHostInterop vk {};
    VkFns fns;
    MemoryMode memoryMode = MemoryMode::OpaqueFd;
    SyncMode syncMode = SyncMode::CpuWait;
    std::string description;
    u32 memoryTypeCount = 0;
    VkMemoryType memoryTypes[VK_MAX_MEMORY_TYPES] {};

    // OpaqueSemaphores
    VkSemaphore vkDone = VK_NULL_HANDLE;   // native -> GL
    VkSemaphore glDone = VK_NULL_HANDLE;   // GL -> native
    GLuint glVkDone = 0;
    GLuint glGlDone = 0;
    bool nativeSignalPending = false;      // vkDone signaled, GL hasn't waited yet
    bool hostSignalPending = false;        // glDone signaled, native hasn't waited yet
    bool hostReading = false;

    // CpuWait
    GLsync hostReadFence = nullptr;

#if defined(__ANDROID__)
    AhbFns ahb;
    EGLDisplay eglDisplay = EGL_NO_DISPLAY;
    VkSemaphore nativeFenceSemaphore = VK_NULL_HANDLE; // signaled by native, exported as sync fd
    VkSemaphore hostFenceSemaphore = VK_NULL_HANDLE;   // imported from GL's native fence fd
    int pendingNativeFenceFd = -1;
#endif
};

//// INIT ////

bool InteropVulkan::init(const HostGLInfo& hostInfo, std::string& error) {
    host = &hostInfo;

    if (!bgfx::vkGetHostInterop(&vk)) {
        error = "Vulkan renderer is not active";
        return false;
    }

    auto getInstance = (PFN_vkGetInstanceProcAddr)vk.getInstanceProcAddr;
    VkInstance instance = (VkInstance)vk.instance;
    VkDevice device = (VkDevice)vk.device;

    fns.getDeviceProcAddr = (PFN_vkGetDeviceProcAddr)vk.getDeviceProcAddr;
    fns.getPhysicalDeviceMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties)getInstance(instance, "vkGetPhysicalDeviceMemoryProperties");
    fns.getPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)getInstance(instance, "vkGetPhysicalDeviceProperties");

#define LOAD_DEVICE(Member, Name) fns.Member = (PFN_##Name)fns.getDeviceProcAddr(device, #Name)
    LOAD_DEVICE(getImageMemoryRequirements, vkGetImageMemoryRequirements);
    LOAD_DEVICE(allocateMemory, vkAllocateMemory);
    LOAD_DEVICE(freeMemory, vkFreeMemory);
    LOAD_DEVICE(bindImageMemory, vkBindImageMemory);
    LOAD_DEVICE(queueWaitIdle, vkQueueWaitIdle);
    LOAD_DEVICE(createSemaphore, vkCreateSemaphore);
    LOAD_DEVICE(destroySemaphore, vkDestroySemaphore);
#if defined(_WIN32)
    LOAD_DEVICE(getMemoryWin32Handle, vkGetMemoryWin32HandleKHR);
    LOAD_DEVICE(getSemaphoreWin32Handle, vkGetSemaphoreWin32HandleKHR);
#else
    LOAD_DEVICE(getMemoryFd, vkGetMemoryFdKHR);
    LOAD_DEVICE(getSemaphoreFd, vkGetSemaphoreFdKHR);
    LOAD_DEVICE(importSemaphoreFd, vkImportSemaphoreFdKHR);
#endif
#if defined(__ANDROID__)
    LOAD_DEVICE(getAndroidHardwareBufferProperties, vkGetAndroidHardwareBufferPropertiesANDROID);
#endif
#undef LOAD_DEVICE

    if (!fns.getImageMemoryRequirements || !fns.allocateMemory || !fns.bindImageMemory || !fns.queueWaitIdle) {
        error = "missing core Vulkan functions";
        return false;
    }

    VkPhysicalDeviceMemoryProperties memoryProperties {};
    fns.getPhysicalDeviceMemoryProperties((VkPhysicalDevice)vk.physicalDevice, &memoryProperties);
    memoryTypeCount = memoryProperties.memoryTypeCount;
    std::memcpy(memoryTypes, memoryProperties.memoryTypes, sizeof(memoryTypes));

    //// MEMORY ////
#if defined(_WIN32)
    memoryMode = MemoryMode::OpaqueWin32;
    if (!fns.getMemoryWin32Handle) {
        error = "VK_KHR_external_memory_win32 is not supported";
        return false;
    }
    if (!host->hasMemoryObjectWin32) {
        error = "GL_EXT_memory_object_win32 is not supported by the OpenGL driver";
        return false;
    }
#elif defined(__ANDROID__)
    memoryMode = MemoryMode::AndroidHardwareBuffer;
    if (!fns.getAndroidHardwareBufferProperties) {
        error = "VK_ANDROID_external_memory_android_hardware_buffer is not supported";
        return false;
    }
    if (!ahb.load() || !host->hasEGLImage) {
        error = "AHardwareBuffer / EGLImage sharing is not available (needs Android 8+)";
        return false;
    }
    eglDisplay = eglGetCurrentDisplay();
#else
    memoryMode = MemoryMode::OpaqueFd;
    if (!fns.getMemoryFd) {
        error = "VK_KHR_external_memory_fd is not supported";
        return false;
    }
    if (!host->hasMemoryObjectFd) {
        error = "GL_EXT_memory_object_fd is not supported by the OpenGL driver";
        return false;
    }
#endif

    //// SYNC ////
    syncMode = SyncMode::CpuWait;
    if (createSemaphores()) {
#if defined(__ANDROID__)
        syncMode = SyncMode::AndroidFences;
#else
        syncMode = SyncMode::OpaqueSemaphores;
#endif
    }

    const char* memoryName =
        memoryMode == MemoryMode::OpaqueWin32 ? "Win32 memory objects" :
        memoryMode == MemoryMode::OpaqueFd    ? "fd memory objects" : "AHardwareBuffer";
    const char* syncName =
        syncMode == SyncMode::CpuWait          ? "CPU sync" :
        syncMode == SyncMode::OpaqueSemaphores ? "GPU semaphores" : "native fences";
    description = std::string("Vulkan (") + memoryName + ", " + syncName + ")";
    return true;
}

bool InteropVulkan::createSemaphores() {
    if (!fns.createSemaphore)
        return false;

    VkDevice device = (VkDevice)vk.device;

#if defined(__ANDROID__)
    if (!ahb.hasFences() || !fns.getSemaphoreFd || !fns.importSemaphoreFd)
        return false;

    VkExportSemaphoreCreateInfo exportInfo { VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO };
    exportInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
    VkSemaphoreCreateInfo info { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    info.pNext = &exportInfo;
    if (fns.createSemaphore(device, &info, nullptr, &nativeFenceSemaphore) != VK_SUCCESS)
        return false;

    VkSemaphoreCreateInfo plain { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    if (fns.createSemaphore(device, &plain, nullptr, &hostFenceSemaphore) != VK_SUCCESS)
        return false;
    return true;
#else
#   if defined(_WIN32)
    if (!host->hasSemaphoreWin32 || !fns.getSemaphoreWin32Handle)
        return false;
    const auto handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#   else
    if (!host->hasSemaphoreFd || !fns.getSemaphoreFd)
        return false;
    const auto handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
#   endif

    auto& gl = glf();

    auto make = [&](VkSemaphore& semaphore, GLuint& glSemaphore) -> bool {
        VkExportSemaphoreCreateInfo exportInfo { VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO };
        exportInfo.handleTypes = handleType;
        VkSemaphoreCreateInfo info { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        info.pNext = &exportInfo;
        if (fns.createSemaphore(device, &info, nullptr, &semaphore) != VK_SUCCESS)
            return false;

        gl.glGenSemaphoresEXT(1, &glSemaphore);
#   if defined(_WIN32)
        VkSemaphoreGetWin32HandleInfoKHR getInfo { VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR };
        getInfo.semaphore = semaphore;
        getInfo.handleType = handleType;
        HANDLE handle = nullptr;
        if (fns.getSemaphoreWin32Handle(device, &getInfo, &handle) != VK_SUCCESS)
            return false;
        gl.glImportSemaphoreWin32HandleEXT(glSemaphore, gl::HANDLE_TYPE_OPAQUE_WIN32_EXT, handle);
        CloseHandle(handle);
#   else
        VkSemaphoreGetFdInfoKHR getInfo { VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR };
        getInfo.semaphore = semaphore;
        getInfo.handleType = handleType;
        int fd = -1;
        if (fns.getSemaphoreFd(device, &getInfo, &fd) != VK_SUCCESS)
            return false;
        gl.glImportSemaphoreFdEXT(glSemaphore, gl::HANDLE_TYPE_OPAQUE_FD_EXT, fd); // GL owns the fd now
#   endif
        return gl.glGetError() == gl::NO_ERROR_;
    };

    GLStateGuard guard(*host);
    if (!make(vkDone, glVkDone) || !make(glDone, glGlDone)) {
        logMessage(LogLevel::Warn, "Bismuth: shared semaphores unavailable, using CPU synchronization");
        return false;
    }
    return true;
#endif
}

InteropVulkan::~InteropVulkan() {
    for (auto& target : targets) {
        if (target)
            destroyTarget(*target);
    }

    VkDevice device = (VkDevice)vk.device;
    if (device && fns.queueWaitIdle)
        fns.queueWaitIdle((VkQueue)vk.queue);

    auto& gl = glf();
    if (glVkDone) gl.glDeleteSemaphoresEXT(1, &glVkDone);
    if (glGlDone) gl.glDeleteSemaphoresEXT(1, &glGlDone);
    if (hostReadFence) gl.glDeleteSync(hostReadFence);

    if (fns.destroySemaphore) {
        if (vkDone) fns.destroySemaphore(device, vkDone, nullptr);
        if (glDone) fns.destroySemaphore(device, glDone, nullptr);
#if defined(__ANDROID__)
        if (nativeFenceSemaphore) fns.destroySemaphore(device, nativeFenceSemaphore, nullptr);
        if (hostFenceSemaphore)   fns.destroySemaphore(device, hostFenceSemaphore, nullptr);
#endif
    }
#if defined(__ANDROID__)
    if (pendingNativeFenceFd >= 0)
        close(pendingNativeFenceFd);
#endif
}

//// TARGETS ////

int32_t InteropVulkan::allocateAndBind(void* userData, void* /*device*/, uint64_t image, void* outMemory) {
    auto* data = static_cast<TargetData*>(userData);
    return data->owner->allocateTargetMemory(*data, fromBits<VkImage>(image), static_cast<VkDeviceMemory*>(outMemory));
}

int32_t InteropVulkan::allocateTargetMemory(TargetData& data, VkImage image, VkDeviceMemory* outMemory) {
    VkDevice device = (VkDevice)vk.device;

    VkMemoryDedicatedAllocateInfo dedicated { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
    dedicated.image = image;

    VkMemoryAllocateInfo allocInfo { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    u32 memoryTypeBits = 0;

#if defined(__ANDROID__)
    VkAndroidHardwareBufferPropertiesANDROID props { VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID };
    VkResult propsResult = fns.getAndroidHardwareBufferProperties(device, data.hardwareBuffer, &props);
    if (propsResult != VK_SUCCESS)
        return propsResult;

    VkImportAndroidHardwareBufferInfoANDROID importInfo { VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID };
    importInfo.buffer = data.hardwareBuffer;
    importInfo.pNext = &dedicated;

    allocInfo.pNext = &importInfo;
    allocInfo.allocationSize = props.allocationSize;
    memoryTypeBits = props.memoryTypeBits;
    data.size = props.allocationSize;
#else
    VkMemoryRequirements requirements {};
    fns.getImageMemoryRequirements(device, image, &requirements);

    VkExportMemoryAllocateInfo exportInfo { VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO };
    exportInfo.handleTypes = data.externalInfo.handleTypes;
    exportInfo.pNext = &dedicated;

    allocInfo.pNext = &exportInfo;
    allocInfo.allocationSize = requirements.size;
    memoryTypeBits = requirements.memoryTypeBits;
    data.size = requirements.size;
#endif

    VkResult result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    for (u32 i = 0; i < memoryTypeCount && result != VK_SUCCESS; i++) {
        if (!(memoryTypeBits & (1u << i)))
            continue;
        if (!(memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            continue;
        allocInfo.memoryTypeIndex = i;
        result = fns.allocateMemory(device, &allocInfo, nullptr, &data.memory);
    }
    if (result != VK_SUCCESS)
        return result;

    result = fns.bindImageMemory(device, image, data.memory, 0);
    if (result != VK_SUCCESS)
        return result;

#if defined(_WIN32)
    VkMemoryGetWin32HandleInfoKHR getInfo { VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR };
    getInfo.memory = data.memory;
    getInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    result = fns.getMemoryWin32Handle(device, &getInfo, &data.memoryHandle);
#elif !defined(__ANDROID__)
    VkMemoryGetFdInfoKHR getInfo { VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR };
    getInfo.memory = data.memory;
    getInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    result = fns.getMemoryFd(device, &getInfo, &data.memoryFd);
#endif
    if (result != VK_SUCCESS)
        return result;

    data.allocated = true;
    *outMemory = data.memory;
    return VK_SUCCESS;
}

bool InteropVulkan::importIntoGL(TargetData& data) {
    auto& gl = glf();
    SharedTarget& target = *data.target;

    GLStateGuard guard(*host);
    gl.glGenTextures(1, &target.glTexture);
    gl.glActiveTexture(gl::TEXTURE0);
    gl.glBindTexture(gl::TEXTURE_2D, target.glTexture);

#if defined(__ANDROID__)
    EGLClientBuffer clientBuffer = ahb.getNativeClientBuffer(data.hardwareBuffer);
    const EGLint attributes[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };
    data.eglImage = ahb.createImage(eglDisplay, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, clientBuffer, attributes);
    if (data.eglImage == EGL_NO_IMAGE_KHR)
        return false;
    gl.glEGLImageTargetTexture2DOES(gl::TEXTURE_2D, data.eglImage);
#else
    gl.glCreateMemoryObjectsEXT(1, &data.glMemoryObject);
    GLint dedicated = 1;
    gl.glMemoryObjectParameterivEXT(data.glMemoryObject, gl::DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
#   if defined(_WIN32)
    gl.glImportMemoryWin32HandleEXT(data.glMemoryObject, data.size, gl::HANDLE_TYPE_OPAQUE_WIN32_EXT, data.memoryHandle);
    CloseHandle(data.memoryHandle);
    data.memoryHandle = nullptr;
#   else
    gl.glImportMemoryFdEXT(data.glMemoryObject, data.size, gl::HANDLE_TYPE_OPAQUE_FD_EXT, data.memoryFd);
    data.memoryFd = -1; // owned by GL now
#   endif
    gl.glTexParameteri(gl::TEXTURE_2D, gl::TEXTURE_TILING_EXT, gl::OPTIMAL_TILING_EXT);
    gl.glTexStorageMem2DEXT(gl::TEXTURE_2D, 1, gl::RGBA8, (GLsizei)target.width, (GLsizei)target.height, data.glMemoryObject, 0);
#endif

    gl.glTexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::NEAREST);
    gl.glTexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::NEAREST);
    gl.glTexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    gl.glTexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);

    GLenum error = gl.glGetError();
    if (error != gl::NO_ERROR_) {
        logf(LogLevel::Error, "Bismuth: importing shared texture into OpenGL failed (0x%x)", error);
        return false;
    }
    return true;
}

bool InteropVulkan::createTarget(SharedTarget& target, u32 width, u32 height) {
    destroyTarget(target);

    auto* data = new TargetData();
    data->owner = this;
    data->target = &target;
    target.platform = data;
    target.width = width;
    target.height = height;
    target.glTarget = gl::TEXTURE_2D;
    // bgfx's Vulkan backend flips the viewport so clip space +Y points up (like
    // Metal), hence image row 0 is the top while GL's texture row 0 is the bottom.
    target.flipY = true;

    data->externalInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
#if defined(_WIN32)
    data->externalInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#elif defined(__ANDROID__)
    data->externalInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;

    AHardwareBuffer_Desc desc {};
    desc.width = width;
    desc.height = height;
    desc.layers = 1;
    desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    desc.usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;
    if (ahb.allocate(&desc, &data->hardwareBuffer) != 0) {
        logMessage(LogLevel::Error, "Bismuth: AHardwareBuffer_allocate failed");
        destroyTarget(target);
        return false;
    }
#else
    data->externalInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif

    data->hooks.userData = data;
    data->hooks.imageCreatePNext = &data->externalInfo;
    data->hooks.allocateAndBind = &InteropVulkan::allocateAndBind;

    target.texture = bgfx::createTexture2D(
        (u16)width, (u16)height, false, 1, bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_RT | BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP
    );
    bgfx::vkSetExternalTexture(target.texture, &data->hooks);
    target.framebuffer = bgfx::createFrameBuffer(1, &target.texture, false);

    // Let bgfx create the image (which calls allocateAndBind).
    bgfx::frame();

    if (!data->allocated || !importIntoGL(*data)) {
        logMessage(LogLevel::Error, "Bismuth: failed to create a shared Vulkan/OpenGL render target");
        destroyTarget(target);
        return false;
    }

    registerTarget(&target);
    return true;
}

void InteropVulkan::destroyTarget(SharedTarget& target) {
    auto* data = static_cast<TargetData*>(target.platform);
    unregisterTarget(&target);

    if (bgfx::isValid(target.framebuffer))
        bgfx::destroy(target.framebuffer);
    if (bgfx::isValid(target.texture)) {
        bgfx::vkSetExternalTexture(target.texture, nullptr);
        bgfx::destroy(target.texture);
    }

    if (target.glTexture || (data && data->glMemoryObject)) {
        auto& gl = glf();
        GLStateGuard guard(*host);
        if (target.glTexture)
            gl.glDeleteTextures(1, &target.glTexture);
        if (data && data->glMemoryObject)
            gl.glDeleteMemoryObjectsEXT(1, &data->glMemoryObject);
    }

    if (data) {
#if defined(_WIN32)
        if (data->memoryHandle) CloseHandle(data->memoryHandle);
#elif !defined(__ANDROID__)
        if (data->memoryFd >= 0) close(data->memoryFd);
#endif
#if defined(__ANDROID__)
        if (data->eglImage != EGL_NO_IMAGE_KHR) ahb.destroyImage(eglDisplay, data->eglImage);
        if (data->hardwareBuffer) ahb.release(data->hardwareBuffer);
#endif
        // The VkDeviceMemory is freed by bgfx together with the texture.
        delete data;
    }

    target = SharedTarget();
}

//// SYNCHRONIZATION ////

u32 InteropVulkan::collectTextures(GLuint* textures, GLenum* layouts) {
    u32 count = 0;
    for (auto* target : targets) {
        if (target && target->glTexture) {
            textures[count] = target->glTexture;
            layouts[count] = gl::LAYOUT_SHADER_READ_ONLY_EXT;
            count++;
        }
    }
    return count;
}

void InteropVulkan::hostWait() {
    if (!nativeSignalPending)
        return;
    nativeSignalPending = false;

    if (syncMode == SyncMode::OpaqueSemaphores) {
        GLuint textures[16];
        GLenum layouts[16];
        u32 count = collectTextures(textures, layouts);
        glf().glWaitSemaphoreEXT(glVkDone, 0, nullptr, count, textures, layouts);
    }
#if defined(__ANDROID__)
    else if (syncMode == SyncMode::AndroidFences && pendingNativeFenceFd >= 0) {
        const EGLint attributes[] = { EGL_SYNC_NATIVE_FENCE_FD_ANDROID, pendingNativeFenceFd, EGL_NONE };
        EGLSyncKHR sync = ahb.createSync(eglDisplay, EGL_SYNC_NATIVE_FENCE_ANDROID, attributes);
        if (sync != EGL_NO_SYNC_KHR) {
            pendingNativeFenceFd = -1; // owned by the sync now
            ahb.waitSync(eglDisplay, sync, 0);
            ahb.destroySync(eglDisplay, sync);
        } else {
            close(pendingNativeFenceFd);
            pendingNativeFenceFd = -1;
            fns.queueWaitIdle((VkQueue)vk.queue);
        }
    }
#endif
}

void InteropVulkan::hostSignal() {
    auto& gl = glf();

    if (syncMode == SyncMode::OpaqueSemaphores) {
        GLuint textures[16];
        GLenum layouts[16];
        u32 count = collectTextures(textures, layouts);
        gl.glSignalSemaphoreEXT(glGlDone, 0, nullptr, count, textures, layouts);
        gl.glFlush();
        hostSignalPending = true;
        return;
    }

#if defined(__ANDROID__)
    if (syncMode == SyncMode::AndroidFences) {
        const EGLint attributes[] = { EGL_SYNC_NATIVE_FENCE_FD_ANDROID, EGL_NO_NATIVE_FENCE_FD_ANDROID, EGL_NONE };
        EGLSyncKHR sync = ahb.createSync(eglDisplay, EGL_SYNC_NATIVE_FENCE_ANDROID, attributes);
        gl.glFlush(); // the fence fd only exists once the fence is flushed
        if (sync != EGL_NO_SYNC_KHR) {
            int fd = ahb.dupNativeFenceFd(eglDisplay, sync);
            ahb.destroySync(eglDisplay, sync);
            if (fd >= 0) {
                VkImportSemaphoreFdInfoKHR importInfo { VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR };
                importInfo.semaphore = hostFenceSemaphore;
                importInfo.flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT;
                importInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
                importInfo.fd = fd;
                if (fns.importSemaphoreFd((VkDevice)vk.device, &importInfo) == VK_SUCCESS) {
                    hostSignalPending = true; // Vulkan owns the fd now
                    return;
                }
                close(fd);
            }
        }
        gl.glFinish();
        return;
    }
#endif

    // CPU sync: remember where GL's reads end.
    if (host->hasSync) {
        if (hostReadFence)
            gl.glDeleteSync(hostReadFence);
        hostReadFence = gl.glFenceSync(gl::SYNC_GPU_COMMANDS_COMPLETE, 0);
        gl.glFlush();
    } else {
        gl.glFinish();
    }
}

void InteropVulkan::beforeNativeFrame() {
    // If the previous frame's targets were never composited, still pair the
    // binary semaphore operations.
    if (nativeSignalPending) {
        beforeHostRead();
        afterHostRead();
    }

    if (syncMode == SyncMode::CpuWait) {
        if (hostReadFence) {
            auto& gl = glf();
            gl.glClientWaitSync(hostReadFence, gl::SYNC_FLUSH_COMMANDS_BIT, 1000000000ull);
            gl.glDeleteSync(hostReadFence);
            hostReadFence = nullptr;
        }
        return;
    }

    uint64_t wait[1];
    uint64_t signal[1];
    u32 waitCount = 0;

#if defined(__ANDROID__)
    if (hostSignalPending)
        wait[waitCount++] = handleBits(hostFenceSemaphore);
    signal[0] = handleBits(nativeFenceSemaphore);
#else
    if (hostSignalPending)
        wait[waitCount++] = handleBits(glDone);
    signal[0] = handleBits(vkDone);
#endif
    hostSignalPending = false;

    bgfx::vkSetFrameSemaphores(wait, waitCount, signal, 1);
}

void InteropVulkan::afterNativeFrame() {
    nativeSignalPending = true;

    if (syncMode == SyncMode::CpuWait) {
        fns.queueWaitIdle((VkQueue)vk.queue);
        return;
    }

#if defined(__ANDROID__)
    if (pendingNativeFenceFd >= 0)
        close(pendingNativeFenceFd);
    pendingNativeFenceFd = -1;

    VkSemaphoreGetFdInfoKHR getInfo { VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR };
    getInfo.semaphore = nativeFenceSemaphore;
    getInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
    if (fns.getSemaphoreFd((VkDevice)vk.device, &getInfo, &pendingNativeFenceFd) != VK_SUCCESS) {
        pendingNativeFenceFd = -1;
        fns.queueWaitIdle((VkQueue)vk.queue);
    }
#endif
}

void InteropVulkan::beforeHostRead() {
    if (hostReading)
        return;
    hostReading = true;

    if (syncMode == SyncMode::CpuWait) {
        nativeSignalPending = false;
        return;
    }

    hostWait();
}

void InteropVulkan::afterHostRead() {
    if (!hostReading)
        return;
    hostReading = false;
    hostSignal();
}

//// FACTORY ////

UPtr<Interop> createVulkanInterop(const HostGLInfo& host, std::string& error) {
    auto interop = makeUnique<InteropVulkan>();
    if (!interop->init(host, error))
        return nullptr;
    return interop;
}

bool prepareVulkanInit(const HostGLInfo& host, std::string& error) {
    if (host.hasDeviceUuid) {
        bgfx::vkSetPreferredDeviceUuid(host.deviceUuid);
    } else {
#if !defined(__ANDROID__)
        // Without the UUID there's no way to make sure both APIs use the same GPU.
        error = "the OpenGL driver doesn't report its device UUID (GL_EXT_memory_object)";
        return false;
#endif
    }
    return true;
}

} // namespace bismuth::gfx

#endif // !__APPLE__
