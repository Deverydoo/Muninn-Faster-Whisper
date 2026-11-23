#include "muninn/version.h"

namespace muninn {

// MUNINN_BUILD_VERSION comes from CMake's project(VERSION), which CMake checks
// against version.h at configure time.
const char* library_version() { return MUNINN_BUILD_VERSION; }

} // namespace muninn
