#include "../src/nexus.hpp"
#include "../src/preview_images.hpp"
#include "../src/toolkit_update_notice.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks = 0;
template<class F> void denied(F&& action, const char* message) {
    ++checks;
    try { action(); } catch (const std::runtime_error& issue) {
        if (std::string(issue.what()) == message) return;
        throw std::runtime_error("Transport reached a later path instead of its local-edition gate.");
    }
    throw std::runtime_error("A local-edition transport did not deny the request.");
}
}
int main() {
    static_assert(drifter::LocalEdition, "This target proves the actual local-edition transports.");
    try {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        denied([&] { drifter::preview_detail::fetch("https://staticdelivery.nexusmods.com/mods/3803/images/994-1.png", 994, deadline); }, drifter::LocalEditionMessage);
        denied([&] { drifter::preview_detail::fetchAvatar("https://avatars.nexusmods.com/62048576/100", 62048576, deadline); }, drifter::LocalEditionMessage);
        denied([&] { drifter::preview_detail::fetchAuthorProfile(deadline); }, drifter::LocalEditionMessage);
        denied([&] { drifter::toolkit_update_notice::fetch(deadline); }, "Toolkit release checks await Nexus registration.");
        std::cout << "Actual local-edition transports denied " << checks << " requests before network setup.\n";
        return 0;
    } catch (const std::exception& issue) { std::cerr << issue.what() << '\n'; return 1; }
}
