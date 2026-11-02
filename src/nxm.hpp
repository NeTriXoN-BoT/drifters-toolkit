#pragma once
#include <filesystem>
#include <string>
#include "../vendor/json.hpp"

namespace drifter {
namespace nxm_detail {
std::wstring registrationCommand(const std::filesystem::path& executable);
bool validTestNamespace(const std::wstring& path);
}

class NxmIntegration {
public:
    explicit NxmIntegration(std::filesystem::path executable);
#ifdef DRIFTER_NXM_TESTING
    // The test constructor accepts only a fresh UUID namespace under the fixed
    // HKCU test branch. It cannot target Classes, UserChoice or HKCR itself.
    NxmIntegration(std::filesystem::path executable, std::wstring testNamespace);
    void failNextRegistrationForTesting();
#endif
    nlohmann::json handle(const nlohmann::json& request);

private:
    std::filesystem::path executable_;
    std::wstring testNamespace_;
#ifdef DRIFTER_NXM_TESTING
    bool failRegistration_ = false;
#endif
    nlohmann::json status();
    nlohmann::json registerHandler();
    nlohmann::json unregisterHandler();
};
}
