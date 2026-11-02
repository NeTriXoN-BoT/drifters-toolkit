#pragma once
#include <filesystem>
namespace drifter {
// Reject crypto overrides, suppress host config and confine provider lookup.
// Call before the first checksum, signature, update-recovery or TLS operation.
void requireSafeCryptoEnvironment();
// Call before QApplication loads plugins or starts QtWebEngineProcess. Ordinary
// distribution builds reject an unexpected adjacent qt.conf instead of adopting it.
// The headless executable verifies its reviewed dependency subset. It does not
// claim to verify unused WebEngine libraries, resources or GUI plugins.
void verifyPortableRuntime(const std::filesystem::path& applicationDirectory, bool cli = false);
}
