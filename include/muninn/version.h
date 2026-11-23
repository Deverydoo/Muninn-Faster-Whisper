#pragma once

// Muninn's version, and a check that the headers you compiled against match
// the muninn.dll that loaded.
//
// Muninn's API passes C++ types (std::string, std::vector, structs) across
// the DLL boundary, so a header from one release with a DLL from another can
// fail in ways that look unrelated (a crash in a destructor, garbage in a
// field). Call muninn::check_version() once at startup to catch that early.

#include <stdexcept>
#include <string>

#include "export.h"

#define MUNINN_VERSION_MAJOR 0
#define MUNINN_VERSION_MINOR 6
#define MUNINN_VERSION_PATCH 0
#define MUNINN_VERSION_STRING "0.6.0"

namespace muninn {

/// The version muninn.dll was built as ("0.6.0").
MUNINN_API const char* library_version();

/// Throws std::runtime_error when the loaded muninn.dll isn't the version
/// these headers belong to.
inline void check_version() {
    const std::string lib = library_version();
    if (lib != MUNINN_VERSION_STRING) {
        throw std::runtime_error("muninn.dll is version " + lib + " but the headers are " +
                                 MUNINN_VERSION_STRING + ": use the DLL from the same release");
    }
}

} // namespace muninn
