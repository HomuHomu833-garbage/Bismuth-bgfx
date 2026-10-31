#include "HostGL.hpp"

#include <TargetConditionals.h>

#if TARGET_OS_IPHONE
#   import <OpenGLES/EAGL.h>
#else
#   import <AppKit/AppKit.h>
#endif

namespace bismuth::gfx {

void* appleCurrentGLContext() {
#if TARGET_OS_IPHONE
    return (__bridge void*)[EAGLContext currentContext];
#else
    return (__bridge void*)[NSOpenGLContext currentContext];
#endif
}

} // namespace bismuth::gfx
