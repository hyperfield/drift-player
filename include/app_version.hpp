#pragma once

// Update these values whenever a new Drift Player release is prepared.
#define DRIFT_APP_VERSION_MAJOR 0
#define DRIFT_APP_VERSION_MINOR 1
#define DRIFT_APP_VERSION_PATCH 0

#define DRIFT_APP_VERSION_STRING "0.1.0"

namespace drift::version
{
inline constexpr int kMajor = DRIFT_APP_VERSION_MAJOR;
inline constexpr int kMinor = DRIFT_APP_VERSION_MINOR;
inline constexpr int kPatch = DRIFT_APP_VERSION_PATCH;
inline constexpr char kVersionString[] = DRIFT_APP_VERSION_STRING;
} // namespace drift::version
