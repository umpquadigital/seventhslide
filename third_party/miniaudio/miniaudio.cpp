// The one translation unit that compiles miniaudio's implementation. The
// MA_NO_* switches are the target's compile definitions (CMakeLists.txt), so
// every file that includes the header sees the same API.
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
