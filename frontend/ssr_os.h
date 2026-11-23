#pragma once
// Platform detection beyond standard compiler macros:
//   SSR_IOS  an iPhone or iPad build
#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#define SSR_IOS 1
#endif
#endif
