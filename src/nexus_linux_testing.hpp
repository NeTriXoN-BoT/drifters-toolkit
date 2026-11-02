#pragma once
#include <functional>
#include <string>
#ifdef DRIFTER_NEXUS_TESTING
namespace drifter::nexus_detail {
// Only compiled into isolated Linux tests. Production never accepts a replacement
// keyring backend and always uses the desktop Secret Service through libsecret.
struct TestKeyring {
    std::function<std::string(const std::string&)> load;
    std::function<void(const std::string&, const std::string&)> store;
    std::function<void(const std::string&)> erase;
};
void setTestKeyring(TestKeyring* backend);
}
#endif
