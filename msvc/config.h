/* config.h.  Manual config for MSVC.  */

#ifndef _MSC_VER
#warn "msvc/config.h shouldn't be included for your development environment."
#error "Please make sure the msvc/ directory is removed from your build path."
#endif

/* DDK directory - automatically supplied when building from DDK */
#ifndef DDKBUILD
#define DDK_DIR "E:/WinDDK/7600.16385.0"
#endif

/* DDK WDF coinstaller version (string) */
#define WDF_VER "01009"

/* 32 bit support */
#define OPT_M32

/* 64 bit support */
#define OPT_M64

/* Debug message logging */
#define ENABLE_DEBUG_LOGGING 1

/* Message logging */
#define ENABLE_LOGGING 1
