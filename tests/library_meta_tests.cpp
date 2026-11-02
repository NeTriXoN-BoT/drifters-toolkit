#include "../src/library_meta.hpp"
#include <iostream>
#include <stdexcept>
using namespace drifter::library_meta;
int main() {
    int count = 0;
    auto check = [&](bool ok) { ++count; if (!ok) throw std::runtime_error("Personal mod metadata check failed."); };
    auto rejects = [&](auto action) { bool failed = false; try { action(); } catch (...) { failed = true; } check(failed); };
    try {
        auto data = update(empty(), "no-recoil", true, "My test setup\nKeep separate.", 1);
        check(data["entries"]["no-recoil"]["favorite"] == true);
        check(data["history"].size() == 2);
        check(update(data, "no-recoil", true, "My test setup\nKeep separate.", 2) == data);
        data = update(data, "no-recoil", false, "", 3);
        check(data["entries"].empty());
        for (int i = 0; i < 120; ++i) data = update(data, "mod", i % 2 == 0, std::to_string(i), i + 4);
        check(data["history"].size() == 100);
        check(data.dump().find("My test setup") == std::string::npos);
        rejects([&] { update(empty(), "../escape", true, "", 1); });
        rejects([&] { update(empty(), "id", true, std::string(4001, 'a'), 1); });
        rejects([&] { update(empty(), "id", true, "bad\x1b[31m", 1); });
        rejects([&] { auto value = empty(); value["entries"]["id"] = {{"favorite", true}, {"note", ""}, {"path", "x"}}; validate(value); });
        rejects([&] { auto value = empty(); value["history"].push_back({{"id", "id"}, {"at", 1}, {"action", "delete"}}); validate(value); });
        rejects([&] { update(empty(), "id", false, "", -1); });
        check(validNote("<script>ordinary personal text</script>")); // Renderers escape text; this is never markup.
        std::cout << "Personal mod metadata checks passed: " << count << '\n';
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
