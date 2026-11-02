#include "../src/save_preview_bridge.hpp"
#include "../src/core_testing.hpp"
#include "../src/platform.hpp"
#include "save_format_fixtures.hpp"
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>

using namespace drifter;
namespace {
unsigned checks = 0, decoderCalls = 0, publicCodecCalls = 0;
bool imageAvailable = true, oversizedImage = false;
bool publicCodecAvailable = true;
std::vector<unsigned char> publicDecoded;
std::function<void()> duringDecode;
std::vector<unsigned char> observedInput;
void check(bool good, const char* reason) { ++checks; if (!good) throw std::runtime_error(reason); }
std::string png() { return save_picture::decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4z8DwHwAFAAH/iZk9HQAAAABJRU5ErkJggg=="); }
void write(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path()); std::ofstream file(path, std::ios::binary); file << bytes;
    if (!file) throw std::runtime_error("Fixture write failed");
}
struct Fixture {
    fs::path root, app, state, saves;
    CoreTestingHooks hooks;
    std::unique_ptr<Manager> manager;
    Fixture() {
        root = fs::temp_directory_path() / ("drifter-preview-bridge-" + std::to_string(GetCurrentProcessId()) + "-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        check(!fs::exists(root), "Fixture is fresh");
        app = root / "app"; state = root / "normal-state"; saves = state / "Saved/SaveGames";
        fs::create_directories(state / "Saved/Paks"); fs::create_directories(saves);
        write(app / "catalog/catalog.json", R"({"schemaVersion":1,"mods":[]})");
        hooks.gameRunning = []()->std::optional<bool> { throw std::runtime_error("No game observation is needed"); };
        setCoreTestingHooks(&hooks);
        manager = std::make_unique<Manager>(app, state, state / "Saved/Paks", ManagerOptions{RecoveryMode::Deferred});
        write(saves / "slot01.sav", "frozen native save bytes");
    }
    ~Fixture() { manager.reset(); setCoreTestingHooks(nullptr); std::error_code error; fs::remove_all(root, error); }
    json list() {
        auto result = manager->handle({{"action", "currentSavesList"}});
        check(result.value("ok", false), "Inventory is readable"); return result.at("data");
    }
    json request(const std::string& id) { return {{"action", "currentSavePreview"}, {"requestId", "fixture"}, {"id", id}}; }
    json preview(const json& request, const std::string& pin = std::string(64, 'a'), const std::string& publicPin = {}) {
        return currentSavePreview(*manager, request, app, pin, publicPin).at("data");
    }
};
template<class F> void refused(F&& action, const char* reason) {
    bool threw = false; try { action(); } catch (const std::exception&) { threw = true; } check(threw, reason);
}
void bridgeContract() {
    Fixture f;
    auto inventory = f.list();
    const auto id = inventory.at("saves")[0].at("id").get<std::string>();
    auto request = f.request(id);
    auto injected = request; injected["path"] = "elsewhere.sav";
    refused([&] { f.preview(injected); }, "Renderer cannot select a path");
    injected = request; injected["bytes"] = "caller save data";
    refused([&] { f.preview(injected); }, "Renderer cannot supply decoder bytes");
    injected = request; injected["id"] = "../outside";
    refused([&] { f.preview(injected); }, "Traversal save ID is refused");
    injected = request; injected["id"] = std::string(64, 'A');
    refused([&] { f.preview(injected); }, "Noncanonical save ID is refused");
    check(decoderCalls == 0, "Invalid requests never reach decoder");
    const auto disabledList = currentSaveInventory(*f.manager, {{"action", "currentSavesList"}}, "").at("data");
    check(!disabledList.at("previewsSupported") && !disabledList.at("saves")[0].at("previewEligible"),
          "Unconfigured GUI does not request expensive unsupported previews");
    check(!disabledList.at("saveEditorAvailable"), "A GUI without the public codec cannot offer save editing");
    const auto wrongEditorPin = currentSaveInventory(*f.manager, {{"action", "currentSavesList"}}, "", std::string(64, 'b')).at("data");
    check(!wrongEditorPin.at("saveEditorAvailable"), "A well-formed but wrong worker pin cannot grant editor capability");
    const auto publicEditor = currentSaveInventory(*f.manager, {{"action", "currentSavesList"}}, "", SaveCodecSha256).at("data");
    check(publicEditor.at("saveEditorAvailable") == SaveEditorEnabled,
          "Only the exact compiled public worker pin exposes the supported editor capability");
    auto unavailable = f.preview(request, "");
    check(!unavailable.at("available") && !unavailable.at("actualGamePreview") && decoderCalls == 0,
          "A build without a helper pin has no decoder authority");
    check(unavailable.at("context") == inventory.at("context") && unavailable.at("id") == id,
          "Unavailable preview retains verified identity");
    check(unavailable.at("reasonCode") == "preview-unavailable" && !unavailable.at("retryable"),
          "A disabled feature should not create an automatic decoder retry loop");
    auto picture = f.preview(request);
    check(picture.at("available") && picture.at("actualGamePreview"), "Validated output has explicit authentic-preview flag");
    check(picture.at("dataUrl") == "data:image/png;base64," + save_picture::encode(png()), "Only sanitized image reaches UI");
    check(std::string(observedInput.begin(), observedInput.end()) == "frozen native save bytes", "Decoder receives native frozen source only");
    imageAvailable = false;
    unavailable = f.preview(request);
    check(!unavailable.at("available") && unavailable.at("reason") == "No picture stored in this save.", "No-image save has an honest fallback");
    imageAvailable = true; oversizedImage = true;
    check(!f.preview(request).at("available"), "Unexpected image dimensions never reach platform codec");
    oversizedImage = false;
    duringDecode = [&] { write(f.saves / "slot01.sav", "replacement save bytes after extraction"); };
    auto stale = f.preview(request);
    duringDecode = {};
    check(!stale.at("available") && !stale.contains("dataUrl"), "Save replacement during decode discards stale screenshot");
    check(stale.at("reasonCode") == "source-changed" && stale.at("retryable"),
          "A changed source must be distinguished from a permanently missing screenshot");
    inventory = f.list();
    request = f.request(inventory.at("saves")[0].at("id").get<std::string>());
    const auto calls = decoderCalls;
    fs::remove(f.saves / "slot01.sav");
    refused([&] { f.preview(request); }, "Deleted save is rejected before decode");
    check(decoderCalls == calls, "Deleted source never reaches helper");
    check(!fs::exists(f.state / "state.json") && !fs::exists(f.state / "save-backups"), "Preview creates no mod state or save backup");
}
void publicCodecContract() {
    Fixture f;
    const auto image = png();
    publicDecoded = save_format_fixtures::fixture(save_format_fixtures::ability(), 1, {image.begin(), image.end()});
    const auto encoded = save_format_fixtures::encoded(publicDecoded);
    write(f.saves / "slot01.sav", std::string(encoded.begin(), encoded.end()));
    auto inventory = f.list(); auto request = f.request(inventory.at("saves")[0].at("id").get<std::string>());
    const auto enabled = currentSaveInventory(*f.manager, {{"action", "currentSavesList"}}, "", std::string(64, 'b')).at("data");
    check(enabled.at("previewsSupported") && enabled.at("saves")[0].at("previewEligible"), "Public codec pin did not enable current-save previews");
    const auto beforePrivate = decoderCalls, beforePublic = publicCodecCalls;
    auto picture = f.preview(request, "", std::string(64, 'b'));
    check(picture.at("available") && picture.at("actualGamePreview"), "Public route did not extract a structurally named game image");
    check(picture.at("dataUrl") == "data:image/png;base64," + save_picture::encode(png()), "Public route bypassed native image sanitization");
    check(publicCodecCalls == beforePublic + 1 && decoderCalls == beforePrivate && observedInput == encoded, "Public route used the wrong decoder or caller-provided input");
    auto injected = request; injected["codecHash"] = std::string(64, 'c');
    refused([&] { f.preview(injected, "", std::string(64, 'b')); }, "Renderer selected the public codec identity");
    const auto calls = publicCodecCalls;
    check(!f.preview(request, "", "invalid").at("available") && publicCodecCalls == calls, "Invalid native pin authorized the public decoder");
    publicCodecAvailable = false;
    const auto failedDecoder = f.preview(request, "", std::string(64, 'b'));
    check(!failedDecoder.at("available") && failedDecoder.at("reasonCode") == "decoder-unavailable" && failedDecoder.at("retryable"),
          "Unavailable public decoder was not marked as a retryable failure");
    publicCodecAvailable = true;
    auto noPicture = save_format_fixtures::decoded();
    const auto noPictureEncoded = save_format_fixtures::encoded(noPicture);
    write(f.saves / "slot01.sav", std::string(noPictureEncoded.begin(), noPictureEncoded.end())); publicDecoded = noPicture;
    inventory = f.list(); request = f.request(inventory.at("saves")[0].at("id").get<std::string>());
    auto absent = f.preview(request, "", std::string(64, 'b'));
    check(!absent.at("available") && absent.at("reason") == "No picture stored in this save.", "Public route fabricated a missing screenshot");
    check(absent.at("reasonCode") == "no-picture" && !absent.at("retryable"),
          "A genuinely absent screenshot must not be repeatedly decoded");
    publicDecoded = save_format_fixtures::withSaveClass(noPicture, "PlayerSaveProfileBP_C");
    const auto profileEncoded = save_format_fixtures::encoded(publicDecoded);
    write(f.saves / "slot01.sav", std::string(profileEncoded.begin(), profileEncoded.end()));
    inventory = f.list(); request = f.request(inventory.at("saves")[0].at("id").get<std::string>());
    const auto profile = f.preview(request, "", std::string(64, 'b'));
    check(!profile.at("available") && profile.at("reasonCode") == "profile-data" && !profile.at("retryable"),
          "Profile classification must follow decoded content rather than its slot filename");
    auto damaged = image; damaged.back() ^= 1;
    publicDecoded = save_format_fixtures::fixture(save_format_fixtures::ability(), 1, {damaged.begin(), damaged.end()});
    const auto brokenEncoded = save_format_fixtures::encoded(publicDecoded);
    write(f.saves / "slot01.sav", std::string(brokenEncoded.begin(), brokenEncoded.end()));
    inventory = f.list(); request = f.request(inventory.at("saves")[0].at("id").get<std::string>());
    const auto invalid = f.preview(request, "", std::string(64, 'b'));
    check(!invalid.at("available") && invalid.at("reasonCode") == "unsupported-format" && !invalid.contains("dataUrl"),
          "A malformed PNG must not be mislabeled as a save without a screenshot");
    publicDecoded = save_format_fixtures::fixture(save_format_fixtures::ability(), 1, {image.begin(), image.end()});
    write(f.saves / "slot01.sav", std::string(encoded.begin(), encoded.end()));
    inventory = f.list(); request = f.request(inventory.at("saves")[0].at("id").get<std::string>());
    duringDecode = [&] { write(f.saves / "slot01.sav", "changed during public decode"); };
    const auto stale = f.preview(request, "", std::string(64, 'b')); duringDecode = {};
    check(!stale.at("available") && !stale.contains("dataUrl"), "Public decoding returned a stale screenshot after source replacement");
    check(!fs::exists(f.state / "state.json") && !fs::exists(f.state / "save-backups"), "Public preview created mod state or a save backup");
}
}
namespace drifter::save_preview_process {
Result decode(const fs::path&, const std::string&, const std::vector<unsigned char>& input) {
    ++decoderCalls; observedInput = input; if (duringDecode) duringDecode();
    if (!imageAvailable) return {false, {}, "No picture stored in this save."};
    const auto bytes = png(); return {true, {bytes.begin(), bytes.end()}, {}};
}
SaveResult decodeSave(const fs::path&, const std::string&, const std::vector<unsigned char>& input) {
    ++publicCodecCalls; observedInput = input; if (duringDecode) duringDecode();
    if (!publicCodecAvailable) return {false, {}, "The save decoder is unavailable."};
    return {true, publicDecoded, {}};
}
}
namespace drifter::preview_detail {
Raster inspectRaster(const std::string&) { return {Raster::Png, oversizedImage ? 513u : 1u, 1}; }
Image decodeThumbnail(const std::string&, const Raster&) { return {png(), 1, 1}; }
}
int main() {
    try { bridgeContract(); publicCodecContract(); std::cout << "Save-preview bridge tests passed " << checks << " checks.\n"; return 0; }
    catch (const std::exception& issue) { std::cerr << "Save-preview bridge tests failed after " << checks << ": " << issue.what() << '\n'; return 1; }
}
