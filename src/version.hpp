#pragma once
// The one place the application version is defined. CMake, the Windows resource
// file and the packaging scripts (scripts/release_layout.py) all read it from here,
// and the in-app updater only installs releases newer than it.
#define DRIFTER_VERSION_MAJOR 1
#define DRIFTER_VERSION_MINOR 0
#define DRIFTER_VERSION_PATCH 9
#define DRIFTER_VERSION "1.0.9"
#ifndef RC_INVOKED
namespace drifter {
inline constexpr const char* AppVersion = DRIFTER_VERSION;
}
#endif
