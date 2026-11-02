#include "../src/core.hpp"
#include "../src/core_testing.hpp"
#include "../src/platform.hpp"
#include "../src/profiles.hpp"
#include "../src/current_save_cli.hpp"
#include "../src/pak_workshop.hpp"
#include "../src/pak_document.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace drifter;
namespace {
unsigned checks=0;
void check(bool good,const char* reason) {++checks;if(!good)throw std::runtime_error(reason);}
void write(const fs::path& path,const std::string& bytes) {
    fs::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary);file<<bytes;
    if(!file)throw std::runtime_error("Fixture write failed");
}
struct Fixture {
    fs::path root,app,state,active,saves;
    CoreTestingHooks hooks;
    std::unique_ptr<Manager> manager;
    Fixture() {
        root=fs::temp_directory_path()/("drifter-current-manager-"+std::to_string(GetCurrentProcessId())+"-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        check(!fs::exists(root),"Fixture is newly created");
        app=root/"app";state=root/"normal-state";active=state/"Saved/Paks";saves=state/"Saved/SaveGames";
        fs::create_directories(active);fs::create_directories(saves);
        write(app/"catalog/catalog.json",R"({"schemaVersion":1,"mods":[]})");
        hooks.gameRunning=[]()->std::optional<bool>{throw std::runtime_error("Process observation must not be needed for read-only saves");};
        setCoreTestingHooks(&hooks);
        manager=std::make_unique<Manager>(app,state,active,ManagerOptions{RecoveryMode::Deferred});
    }
    ~Fixture(){manager.reset();setCoreTestingHooks(nullptr);std::error_code error;fs::remove_all(root,error);}
    json list() {
        const auto result=manager->handle({{"action","currentSavesList"}});
        check(result.value("ok",false),"Current save list is a native successful read-only action");return result.at("data");
    }
};
void currentReadOnlyRoute() {
    Fixture f;
    check(f.list().at("saves").empty(),"New installation has an empty ready inventory");
    write(f.saves/"slot01.sav","real current save bytes");
    write(f.active/"unreadable.pak","intentionally invalid PAK: list must not parse it");
    write(f.state/"approved-local-catalog.json","intentionally malformed: list must not reload this catalog");
    unsigned pakReads=0;
    f.hooks.checkpoint=[&](const char* name){if(std::string(name).rfind("index-cache-",0)==0)++pakReads;};
    const auto list=f.list();
    check(list.at("status")=="ready" && list.at("saves").size()==1,"Current saves work independently of unrelated broken PAK/catalog state");
    check(pakReads==0 && !fs::exists(f.state/"index-cache.json"),"Polling performs no PAK scan or index-cache write");
    const auto id=list.at("saves")[0].at("id").get<std::string>();
    const auto source=f.manager->frozenSavePreviewSource(id);
    check(source.context==list.at("context") && source.id==id && std::string(source.bytes.begin(),source.bytes.end())=="real current save bytes",
          "Native decoder source is context-bound and bypasses unrelated catalogue work");
    const auto inspected=f.manager->frozenSaveInspectionSource(id);
    check(inspected.id==source.id && inspected.context==source.context && inspected.bytes==source.bytes,
          "Manager inspection must reuse stable read-only identity authority without process observation or unrelated catalogue work");
    f.hooks.gameRunning=[]()->std::optional<bool>{return true;};
    check(f.manager->frozenSaveInspectionSource(id).bytes==source.bytes,
          "Manager inspection must remain available while Days Gone is running");
    bool editRefused=false;try {f.manager->frozenSaveEditorSource(id);}catch(const std::exception&){editRefused=true;}
    check(editRefused,"An inspection route must not relax the separate stopped-game editing authority");
    const auto injected=f.manager->handle({{"action","currentSavesList"},{"path",f.saves.u8string()}});
    check(!injected.value("ok",true),"Renderer paths cannot redirect the current-save inventory");
    check(!fs::exists(f.state/"state.json") && !fs::exists(f.state/"save-backups/index.json"),"Read-only current saves do not write manager state or create a backup");
    fs::remove(f.saves/"slot01.sav");
    bool refused=false;try {f.manager->frozenSavePreviewSource(id);}catch(const std::exception&){refused=true;}
    check(refused,"Deleted current save source is refused before decoder access");
    check(f.list().at("saves").empty(),"Deleted save disappears without a toolkit restart");
}
void independentSaveManagement() {
    Fixture f;
    f.hooks.gameRunning=[]()->std::optional<bool>{return false;};
    write(f.saves/"slot01.sav","selected live save");
    write(f.saves/"slot02.sav","other live save");
    const auto initial=f.list();
    const auto id=initial["saves"][0]["name"]=="slot01.sav" ? initial["saves"][0]["id"] : initial["saves"][1]["id"];
    write(f.active/"broken.pak","unrelated broken PAK");
    write(f.state/"approved-local-catalog.json","unrelated malformed local approval");
    auto callSave=[&](const json& request) {
        const auto result=f.manager->handle(request);
        check(result.value("ok",false),"Independent save management succeeds without reading unrelated PAK/catalog data");
        return result.at("data");
    };
    const auto plan=callSave({{"action","currentSaveDeleteReview"},{"id",id}});
    const auto removed=callSave({{"action","currentSaveDelete"},{"token",plan["token"]},{"confirmed",true}});
    check(removed["deleted"]==true && !fs::exists(f.saves/"slot01.sav") && fs::exists(f.saves/"slot02.sav"),
          "Native current-save deletion removes only its opaque selected file");
    const auto backups=callSave({{"action","saveBackupsList"}});
    check(backups["settings"]["automatic"]==false && backups["backups"].size()==1 &&
          backups["backups"][0]["reason"]=="before-delete-save", "Actual deletion forces a full safety backup while automatic backups remain off");
    const auto restore=callSave({{"action","saveBackupReview"},{"id",removed["safetyBackupId"]}});
    const auto restored=callSave({{"action","saveBackupRestore"},{"token",restore["token"]},{"confirmed",true}});
    check(restored["restored"]==true && fs::file_size(f.saves/"slot01.sav")==18 && fs::exists(f.saves/"slot02.sav"),
          "The safety snapshot can undo deletion through existing Restore despite unrelated catalog damage");
    const auto input=f.root/"downloads/imported.sav";
    write(input,"inert imported save");
    f.hooks.gameRunning=[]()->std::optional<bool>{return true;};
    const auto imported=f.manager->inspectSaveImport({input});
    check(imported["imported"]==true && !fs::exists(f.saves/"imported.sav"),
          "Native save imports stay inert and work while the game is running");
    const auto deletion=callSave({{"action","backupDeleteReview"},{"id",imported["id"]}});
    check(callSave({{"action","backupDelete"},{"token",deletion["token"]},{"confirmed",true}})["deleted"]==true,
          "Native private-backup deletion is independent of game-running and unrelated catalog state");
    const auto blocked=f.manager->handle({{"action","currentSaveDeleteReview"},{"id",f.list()["saves"][0]["id"]}});
    check(!blocked.value("ok",false),"Live-save deletion remains blocked while Days Gone runs");
}
json call(Manager& manager,const json& request) {
    auto value=manager.handle(request);check(value.value("ok",false),"Portable save-link Manager action succeeds");return value.at("data");
}
json call(Profiles& profiles,const json& request) {
    auto value=profiles.handle(request);if(!value.value("ok",false))throw std::runtime_error(value.value("error","Profile fixture failed"));
    ++checks;return value.at("data");
}
std::string saveId(Fixture& fixture, const std::string& name) {
    const auto inventory = fixture.list();
    for (const auto& row : inventory.at("saves")) if (row.at("name") == name) return row.at("id");
    throw std::runtime_error("Selected save fixture is missing");
}
std::string readBytes(const fs::path& path) { std::ifstream file(path, std::ios::binary); return {std::istreambuf_iterator<char>(file), {}}; }
template<class F> void refuses(F action, const char* message) { bool failed = false; try { action(); } catch (...) { failed = true; } check(failed, message); }
void nativeWorkshopSaveProtection() {
    Fixture f; write(f.saves/"slot01.sav", "untouched save progress");
    auto has = [](const std::vector<fs::path>& roots, const fs::path& wanted) {
        return std::any_of(roots.begin(), roots.end(), [&](const auto& root) { return nativePathKey(root) == nativePathKey(wanted); });
    };
    const auto roots = f.manager->workshopProtectedSaveRoots();
    check(has(roots, f.saves) && has(roots, f.saves.parent_path()),
          "Native Workshop export protection must include the exact live SaveGames and complete Saved account root");
    check(readBytes(f.saves/"slot01.sav") == "untouched save progress" && !fs::exists(f.state/"save-backups/index.json"),
          "Reading native protected roots must not back up or change a save");
    const auto custom = f.root/"custom-drive", customState = f.root/"custom-state";
    fs::create_directories(custom/"Mods"); fs::create_directories(custom/"SaveGames");
    Manager customManager(f.app, customState, custom/"Mods", ManagerOptions{RecoveryMode::Deferred});
    const auto customRoots = customManager.workshopProtectedSaveRoots();
    check(has(customRoots, custom/"SaveGames") && !has(customRoots, custom) && !has(customRoots, custom.root_path()),
          "A custom Mods directory must not turn its parent or entire drive into a guessed protected save root");
    fs::create_directories(custom/"1234567890/SaveGames");
    refuses([&] { customManager.workshopProtectedSaveRoots(); }, "Ambiguous custom save accounts must not silently disable export protection");
    fs::remove(custom/"1234567890/SaveGames"); fs::remove(custom/"1234567890"); fs::remove(custom/"SaveGames");
    refuses([&] { customManager.workshopProtectedSaveRoots(); }, "An unknown custom layout with missing save roots must request configuration");

    const auto saved = f.root/"other-game/BendGame/Saved", account = saved/"1234567890";
    fs::create_directories(saved/"Paks"); fs::create_directories(account/"SaveGames");
    Manager accountManager(f.app, f.root/"account-state", saved/"Paks", ManagerOptions{RecoveryMode::Deferred});
    const auto accountRoots = accountManager.workshopProtectedSaveRoots();
    check(has(accountRoots, account/"SaveGames") && has(accountRoots, account) && has(accountRoots, saved),
          "Native account roots must protect the account parent and every account under a known Saved layout");
    fs::create_directories(saved/"0987654321/SaveGames");
    const auto allAccountRoots = accountManager.workshopProtectedSaveRoots();
    check(has(allAccountRoots, saved) && has(allAccountRoots, account/"SaveGames") && !has(allAccountRoots, f.root),
          "Multiple accounts under a known Saved layout must allow independent exports while protecting every account and the bound prior root");

    const auto missingSaved = f.root/"new-game/BendGame/Saved";
    fs::create_directories(missingSaved/"Paks");
    Manager missingManager(f.app, f.root/"missing-state", missingSaved/"Paks", ManagerOptions{RecoveryMode::Deferred});
    check(has(missingManager.workshopProtectedSaveRoots(), missingSaved),
          "A known native Saved/Paks layout must remain protected before SaveGames exists");
    const auto offlineState = f.root/"offline-state";
    fs::create_directories(offlineState/"offline-paks");
    Manager offlineManager(f.app, offlineState, offlineState/"offline-paks", ManagerOptions{RecoveryMode::Deferred});
    const auto offlineRoots = offlineManager.workshopProtectedSaveRoots();
    check(has(offlineRoots, offlineState) && !has(offlineRoots, f.root),
          "Offline Workshop authoring must stay usable while protecting its application-owned state, without guessing a game drive");
}
void actualWorkshopExportsRespectSaveRoots() {
    Fixture f;
    const auto saved = f.root/"external-userdata/BendGame/Saved", slots = saved/"SaveGames";
    fs::create_directories(saved/"Paks"); write(slots/"slot01.sav", "existing game progress");
    Manager manager(f.app, f.root/"separate-toolkit-state", saved/"Paks", ManagerOptions{RecoveryMode::Deferred});
    auto roots = manager.workshopProtectedSaveRoots();
    const auto source = f.root/"source.bin"; write(source, "exact opaque source bytes");
    const auto originalSave = readBytes(slots/"slot01.sav");
    PakWorkshop workshop(f.root/"authoring-projects");
    auto opened = workshop.handle({{"action", "workshopNew"}});
    check(opened.value("ok", false), "Actual protected-export fixture must open a native empty Workshop");
    const auto added = workshop.addFile({{"action", "__workshopAddFile"},
        {"session", opened.at("data").at("session").at("id")}, {"revision", 0},
        {"virtualPath", "BendGame/Content/ProtectedExportFixture.bin"}}, source);
    const auto asset = added.at("asset").at("id").get<std::string>();
    auto review = [&](bool raw) {
        const auto state = workshop.handle({{"action", "workshopStatus"}}).at("data").at("session");
        json request{{"action", raw ? "workshopExtractReview" : "workshopBuildReview"},
                     {"session", state.at("id")}, {"revision", state.at("revision")}};
        if (raw) request["asset"] = asset;
        const auto result = workshop.handle(request);
        check(result.value("ok", false), "Actual protected-export fixture requires a fresh native review");
        return result.at("data").at("token").get<std::string>();
    };
    auto exportTo = [&](bool raw, const fs::path& destination) {
        const auto token = review(raw);
        return raw ? workshop.exportAssetReviewed(token, destination, roots)
                   : workshop.exportReviewed(token, destination, roots);
    };
    for (bool raw : {false, true}) {
        const auto destination = slots/(raw ? "blocked-raw.bin" : "blocked-package.pak");
        refuses([&] { exportTo(raw, destination); }, "Actual Manager save roots must refuse both raw and PAK exports into live saves");
        check(!fs::exists(destination) && readBytes(slots/"slot01.sav") == originalSave,
              "Refused actual Workshop export created an artifact or changed an existing game save");
    }
    const auto outside = f.root/"exports"; fs::create_directories(outside);
    check(exportTo(true, outside/"allowed.bin").at("exported") && readBytes(outside/"allowed.bin") == readBytes(source),
          "Actual raw export outside native protected save roots must remain available and byte exact");
    check(exportTo(false, outside/"allowed.pak").at("exported"), "Actual PAK export outside native protected roots must remain available");
    const auto package = pak_document::open(outside/"allowed.pak");
    check(package.assets.size() == 1 && std::string(package.assets[0].bytes.begin(), package.assets[0].bytes.end()) == readBytes(source),
          "Allowed native PAK export failed its actual decoded-byte check");

    const auto accountSaved = f.root/"account-userdata/BendGame/Saved", first = accountSaved/"1234567890", second = accountSaved/"0987654321";
    fs::create_directories(accountSaved/"Paks"); write(first/"SaveGames/slot01.sav", "first account progress");
    Manager accounts(f.app, f.root/"account-toolkit-state", accountSaved/"Paks", ManagerOptions{RecoveryMode::Deferred});
    roots = accounts.workshopProtectedSaveRoots();
    refuses([&] { exportTo(true, first/"blocked-account-parent.bin"); }, "A resolved account parent must reject raw Workshop export");
    check(!fs::exists(first/"blocked-account-parent.bin"), "A raw export created an artifact inside a game account parent");
    write(second/"SaveGames/slot02.sav", "second account progress");
    roots = accounts.workshopProtectedSaveRoots(); // Known Saved protects both ambiguous accounts.
    for (const auto& account : {first, second}) for (bool raw : {false, true}) {
        const auto destination = account/"SaveGames"/(raw ? "blocked.bin" : "blocked.pak");
        refuses([&] { exportTo(raw, destination); }, "Every account under an ambiguous known Saved root must reject actual raw/PAK export");
        check(!fs::exists(destination), "A refused multi-account export left an artifact");
    }
    check(exportTo(true, outside/"multi-account-allowed.bin").at("exported") &&
          exportTo(false, outside/"multi-account-allowed.pak").at("exported"),
          "Independent Workshop exports must not be blocked by multiple safely protected game accounts");
    check(readBytes(first/"SaveGames/slot01.sav") == "first account progress" &&
          readBytes(second/"SaveGames/slot02.sav") == "second account progress" && readBytes(source) == "exact opaque source bytes",
          "Protected-root checks or allowed exports changed source/save data");
}
void stableCliSaveSelection() {
    Fixture f;
    write(f.saves / "slot01.sav", "same exact native save across CLI sessions");
    const auto original = current_save_cli::inventory(*f.manager);
    check(original.value("ok", false) && original.at("data").at("saves").size() == 1,
          "CLI inventory did not retain the native result envelope");
    const auto row = original.at("data").at("saves").at(0);
    const auto alias = row.at("id").get<std::string>();
    check(current_save_cli::validAlias(alias) && !row.contains("cliId") && original.dump().find(f.root.u8string()) == std::string::npos,
          "CLI output exposes only its bounded alias, without duplicate alias fields or native private paths");
    const auto originalNative = saveId(f, "slot01.sav");
    check(alias != originalNative, "CLI selection reused the GUI authentication identity");
    refuses([&] { f.manager->frozenSavePreviewSource(alias); }, "CLI aliases directly authorized a GUI source read");
    Manager next(f.app, f.state, f.active, ManagerOptions{RecoveryMode::Deferred});
    const auto nextInventory = current_save_cli::inventory(next);
    check(nextInventory.at("data").at("saves").at(0).at("id") == alias,
          "A fresh Manager could not reuse a CLI alias for the same exact native save");
    const auto resolved = current_save_cli::resolve(next, alias);
    check(resolved != originalNative && resolved != alias,
          "CLI lookup did not obtain the new Manager's process-private native ID");
    const auto source = next.frozenSavePreviewSource(resolved);
    check(std::string(source.bytes.begin(), source.bytes.end()) == "same exact native save across CLI sessions",
          "Fresh CLI alias resolution did not reach its exact frozen native save");
    refuses([&] { current_save_cli::resolve(next, originalNative); }, "An old process-private ID was adopted as a stable CLI alias");
    refuses([&] { current_save_cli::resolve(next, std::string(64, '0')); }, "An unknown well-formed CLI alias was adopted");
    refuses([&] { current_save_cli::resolve(next, f.saves.u8string()); }, "A filesystem path bypassed CLI alias lookup");
    fs::rename(f.saves / "slot01.sav", f.saves / "retained.bin");
    write(f.saves / "slot01.sav", "replacement save at the same name");
    refuses([&] { current_save_cli::resolve(next, alias); }, "A stale CLI alias resolved after file replacement");
    const auto changed = current_save_cli::inventory(next).at("data").at("saves").at(0).at("id").get<std::string>();
    check(changed != alias && !current_save_cli::resolve(next, changed).empty(),
          "A replaced save did not get a new usable CLI alias");
    write(f.state / "Saved/76561198000000000/SaveGames/account.sav", "other account");
    refuses([&] { current_save_cli::resolve(next, changed); }, "An ambiguous account retained a CLI selection");
    check(!fs::exists(f.state / "state.json") && !fs::exists(f.state / "save-backups"),
          "CLI inventory/resolution persisted a key, manager state or backup");
}
void stableCliDeletionReview() {
    Fixture f; f.hooks.gameRunning = [] { return std::optional<bool>(false); };
    write(f.saves / "slot01.sav", "selected CLI save");
    write(f.saves / "slot02.sav", "unrelated current save");
    const auto listed = current_save_cli::inventory(*f.manager).at("data").at("saves");
    std::string alias;
    for (const auto& row : listed) if (row.at("name") == "slot01.sav") alias = row.at("id");
    check(!alias.empty(), "CLI deletion fixture has a stable selection.");
    const auto reviewed = current_save_cli::deletionReview(*f.manager, {{"action", "currentSaveDeleteReview"}, {"id", alias}});
    check(reviewed.value("ok", false) && reviewed.at("data").at("id") == alias &&
          reviewed.at("data").at("operation") == "delete-current-save" && reviewed.at("data").at("safetyBackupRequired"),
          "CLI review did not map the authenticated native plan back to the selected stable alias.");
    check(fs::exists(f.saves / "slot01.sav") && !fs::exists(f.state / "save-backups"),
          "CLI deletion review must not remove the save or create its safety backup before confirmation.");
    const auto deleted = call(*f.manager, {{"action", "currentSaveDelete"}, {"token", reviewed.at("data").at("token")}, {"confirmed", true}});
    check(deleted.at("deleted") && !fs::exists(f.saves / "slot01.sav") && fs::exists(f.saves / "slot02.sav"),
          "The remapped CLI review did not retain its native one-use exact-file authority.");
}
void individualImportExport() {
    Fixture f; f.hooks.gameRunning=[] {return std::optional<bool>(false);};
    write(f.saves/"slot01.sav", "before selected slot"); write(f.saves/"slot02.sav", "other progress stays");
    const auto source=f.root/"downloads/shared.sav", exported=f.root/"exports/private.sav";
    write(source, "imported frozen progress"); fs::create_directories(exported.parent_path());
    auto id=saveId(f,"slot01.sav");
    const auto output=f.manager->exportCurrentSave(id,exported);
    check(output["exported"]==true && readBytes(exported)=="before selected slot", "Individual export did not copy the selected frozen bytes");
    refuses([&]{f.manager->exportCurrentSave(id,exported);}, "Export overwrote an existing destination");
    refuses([&]{f.manager->exportCurrentSave(id,f.saves/"newslot.sav");}, "Export bypassed save replacement review inside the live save root");
    check(!fs::exists(f.saves/"newslot.sav"), "A refused export created a live save");
    const auto plan=f.manager->inspectCurrentSaveImport(id,source);
    check(plan["operation"]=="import-current-save" && plan["targetName"]=="slot01.sav" && plan["sourceName"]=="shared.sav" &&
          plan["safetyBackupRequired"]==true && plan.dump().find(f.root.u8string())==std::string::npos,
          "Individual import review exposed native paths or lost its exact target/backup requirement");
    check(!f.manager->handle({{"action","currentSaveReplace"},{"token",plan["token"]},{"confirmed",false}}).value("ok",false) &&
          readBytes(f.saves/"slot01.sav")=="before selected slot", "An unconfirmed replacement changed a save");
    write(source,"download changed after review");
    const auto imported=call(*f.manager,{{"action","currentSaveReplace"},{"token",plan["token"]},{"confirmed",true}});
    check(imported["replaced"]==true && readBytes(f.saves/"slot01.sav")=="imported frozen progress" &&
          readBytes(f.saves/"slot02.sav")=="other progress stays" && !fs::exists(f.state/"save-restore.json"),
          "One-slot import changed another save, depended on the changed download or left a committed journal");
    const auto backups=call(*f.manager,{{"action","saveBackupsList"}});
    check(backups["settings"]["automatic"]==false && backups["backups"].size()==1 && backups["backups"][0]["reason"]=="before-import-save",
          "Individual import failed to force its safety backup with automatic backups off");
    const auto backupRoot=f.state/"save-backups/entries"/imported["safetyBackupId"].get<std::string>()/"files";
    check(readBytes(backupRoot/"slot01.sav")=="before selected slot" && readBytes(backupRoot/"slot02.sav")=="other progress stays",
          "One-slot replacement did not back up the complete current collection first");
    check(!f.manager->handle({{"action","currentSaveReplace"},{"token",plan["token"]},{"confirmed",true}}).value("ok",false), "A used replacement token was replayable");
    id=saveId(f,"slot01.sav"); const std::vector<unsigned char> edited{'e','d','i','t','e','d'};
    const auto edit=f.manager->prepareCurrentSaveReplacement(id,edited);
    call(*f.manager,{{"action","currentSaveReplace"},{"token",edit["token"]},{"confirmed",true}});
    const auto editedBackups=call(*f.manager,{{"action","saveBackupsList"}});
    bool found=false; for(const auto& backup:editedBackups["backups"]) if(backup["reason"]=="before-edit-save")found=true;
    check(found && readBytes(f.saves/"slot01.sav")=="edited" && readBytes(f.saves/"slot02.sav")=="other progress stays",
          "Native edited-byte replacement failed its separate safety reason or touched another slot");
}
void individualReplacementDriftAndRecovery() {
    Fixture f; f.hooks.gameRunning=[] {return std::optional<bool>(false);};
    write(f.saves/"slot01.sav","original slot"); write(f.saves/"slot02.sav","other original"); write(f.root/"import.sav","replacement bytes");
    auto plan=f.manager->inspectCurrentSaveImport(saveId(f,"slot01.sav"),f.root/"import.sav");
    write(f.saves/"slot01.sav","newer current progress");
    check(!f.manager->handle({{"action","currentSaveReplace"},{"token",plan["token"]},{"confirmed",true}}).value("ok",false) &&
          readBytes(f.saves/"slot01.sav")=="newer current progress", "A stale selected-save review overwrote newer progress");
    plan=f.manager->inspectCurrentSaveImport(saveId(f,"slot01.sav"),f.root/"import.sav");
    f.hooks.gameRunning=[] {return std::optional<bool>(true);};
    check(!f.manager->handle({{"action","currentSaveReplace"},{"token",plan["token"]},{"confirmed",true}}).value("ok",false), "A running game did not block save replacement");
    f.hooks.gameRunning=[] {return std::optional<bool>(false);};
    plan=f.manager->inspectCurrentSaveImport(saveId(f,"slot01.sav"),f.root/"import.sav");
    f.hooks.checkpoint=[&](const char* point){if(std::string(point)=="save-replace-after-backup")write(f.saves/"slot02.sav","external newer other progress");};
    check(!f.manager->handle({{"action","currentSaveReplace"},{"token",plan["token"]},{"confirmed",true}}).value("ok",false) &&
          readBytes(f.saves/"slot01.sav")=="newer current progress" && readBytes(f.saves/"slot02.sav")=="external newer other progress",
          "A concurrent other-save change was ignored after the full safety backup");
    f.hooks.checkpoint={};
    plan=f.manager->inspectCurrentSaveImport(saveId(f,"slot01.sav"),f.root/"import.sav");
    f.hooks.checkpoint=[](const char* point){if(std::string(point)=="save-replace-original-held")throw std::runtime_error("Synthetic replacement interruption");};
    check(!f.manager->handle({{"action","currentSaveReplace"},{"token",plan["token"]},{"confirmed",true}}).value("ok",false), "Interrupted replacement did not exercise its rollback");
    f.hooks.checkpoint={};
    check(readBytes(f.saves/"slot01.sav")=="newer current progress" && readBytes(f.saves/"slot02.sav")=="external newer other progress" &&
          !fs::exists(f.state/"save-restore.json"), "Replacement interruption lost the original slot, another save or rollback state");
}
void transactionalDeletionRecovery() {
    Fixture f; bool running=false;
    f.hooks.gameRunning=[&] {return std::optional<bool>(running);};
    write(f.saves/"slot01.sav","original selected progress");
    write(f.saves/"slot02.sav","other progress remains");
    auto plan=call(*f.manager,{{"action","currentSaveDeleteReview"},{"id",saveId(f,"slot01.sav")}});
    f.hooks.checkpoint=[&](const char* point) {
        if (std::string(point)=="save-delete-original-held") running=true;
    };
    check(!f.manager->handle({{"action","currentSaveDelete"},{"token",plan["token"]},{"confirmed",true}}).value("ok",false),
          "Interrupted transactional deletion did not preserve a recovery journal");
    f.hooks.checkpoint={}; running=false;
    check(fs::exists(f.state/"save-restore.json") && !fs::exists(f.saves/"slot01.sav") &&
          readBytes(f.saves/"slot02.sav")=="other progress remains",
          "Interrupted deletion lost its recovery evidence or modified another current save");
    f.manager.reset();
    f.manager=std::make_unique<Manager>(f.app,f.state,f.active,ManagerOptions{RecoveryMode::Deferred});
    const auto recovery=call(*f.manager,{{"action","saveBackupRecoveryReview"}});
    const auto restored=call(*f.manager,{{"action","saveBackupRecover"},{"token",recovery["token"]},{"confirmed",true}});
    check(restored.at("recovered") && readBytes(f.saves/"slot01.sav")=="original selected progress" &&
          readBytes(f.saves/"slot02.sav")=="other progress remains" && !fs::exists(f.state/"save-restore.json"),
          "A fresh loader could not recover a held deletion through its existing save-recovery flow");
    plan=call(*f.manager,{{"action","currentSaveDeleteReview"},{"id",saveId(f,"slot01.sav")}});
    f.hooks.checkpoint=[](const char* point) {if(std::string(point)=="save-delete-original-held")throw std::runtime_error("Deletion interruption");};
    check(!f.manager->handle({{"action","currentSaveDelete"},{"token",plan["token"]},{"confirmed",true}}).value("ok",false),
          "Immediate deletion rollback fixture did not reach its held-file checkpoint");
    f.hooks.checkpoint={};
    check(readBytes(f.saves/"slot01.sav")=="original selected progress" && !fs::exists(f.state/"save-restore.json"),
          "Immediate deletion rollback failed to restore the exact selected file");
}
#ifndef _WIN32
void deletionLateNameReplacement() {
    Fixture f; f.hooks.gameRunning=[] {return std::optional<bool>(false);};
    write(f.saves/"slot01.sav","original selected progress");
    write(f.saves/"slot02.sav","other progress remains");
    const auto plan=call(*f.manager,{{"action","currentSaveDeleteReview"},{"id",saveId(f,"slot01.sav")}});
    bool swapped=false;
    f.hooks.checkpoint=[&](const char* point) {
        if (!swapped && std::string(point)=="save-file-before-native-move") {
            swapped=true;
            fs::rename(f.saves/"slot01.sav",f.saves/"externally-retained.bin");
            write(f.saves/"slot01.sav","new unbacked progress");
        }
    };
    check(!f.manager->handle({{"action","currentSaveDelete"},{"token",plan["token"]},{"confirmed",true}}).value("ok",false),
          "A late native source-name replacement was not refused");
    f.hooks.checkpoint={};
    check(swapped && readBytes(f.saves/"slot01.sav")=="new unbacked progress" &&
          readBytes(f.saves/"externally-retained.bin")=="original selected progress" &&
          readBytes(f.saves/"slot02.sav")=="other progress remains" && fs::exists(f.state/"save-restore.json"),
          "A late replacement was deleted, overwritten, hidden without recovery evidence or changed another save");
    bool backedUp=false;
    const auto backups=call(*f.manager,{{"action","saveBackupsList"}});
    for (const auto& backup:backups.at("backups")) {
        const auto path=f.state/"save-backups/entries"/backup.at("id").get<std::string>()/"files/slot01.sav";
        if (fs::exists(path) && readBytes(path)=="original selected progress") backedUp=true;
    }
    check(backedUp,"The refused source-name race did not retain the complete prior safety backup");
}
#endif
void portableProfileBackupHooks() {
    Fixture f;
    f.hooks.gameRunning=[] {return std::optional<bool>(false);};
    const auto game=f.root/"game";
    write(game/"DaysGone.exe","fixture only");
    write(game/"BendGame/Content/Paks/BendGame-WindowsNoEditor.pak","untouched base");
    write(f.saves/"slot01.sav","before the profile");
    call(*f.manager,{{"action","saveSettings"},{"settings",{{"gamePath",game.u8string()}}}});
    call(*f.manager,{{"action","saveBackupsSettings"},{"automatic",true},{"keep",5}});
    Profiles profiles(f.state,*f.manager);
    call(profiles,{{"action","profilesSave"},{"name","Portable fixture"}});
    auto prepare=[&] {
        const auto plan=call(profiles,{{"action","profileLaunchPlan"},{"name","Portable fixture"}});
        check(plan.at("canLaunch").get<bool>(),"Portable reviewed profile is ready");
        return call(profiles,{{"action","profilePrepareLaunch"},{"name","Portable fixture"},{"planToken",plan.at("planToken")},{"confirmed",true}});
    };
    const auto prepared=prepare();
    check(prepared.at("saveBackup").at("profiles")[0].at("stage")=="before-launch","Native reviewed launch supplies a before-launch backup role");
    write(f.saves/"slot01.sav","progress made during the profile");
    const auto finished=call(profiles,{{"action","profileFinishSession"}});
    check(!finished.contains("warning") && finished.at("saveBackup").at("profiles")[0].at("stage")=="after-session" &&
              !fs::exists(f.state/"profiles-session.json"),"Successful portable session restore records an after-session save backup");
    check(finished.at("saveBackup").at("id")!=prepared.at("saveBackup").at("id"),"Changed progress is backed up separately without swapping game saves");
    prepare();
    write(f.saves/"slot01.sav","later progress must survive a backup warning");
    f.hooks.checkpoint=[](const char* phase){if(std::string(phase)=="save-backup-committed")throw std::runtime_error("Synthetic post-session backup failure");};
    const auto warned=call(profiles,{{"action","profileFinishSession"}});
    f.hooks.checkpoint={};
    check(warned.contains("warning") && !fs::exists(f.state/"profiles-session.json") && !warned.at("session").at("active").get<bool>(),
          "A failed optional post-session save backup cannot undo a completed profile restoration");
    const auto inventory=f.list();const auto source=f.manager->frozenSavePreviewSource(inventory.at("saves")[0].at("id"));
    check(std::string(source.bytes.begin(),source.bytes.end())=="later progress must survive a backup warning","Profile-linked backups never replace shared live game saves");
    const auto reportPath=f.root/"support-report.json";
    call(*f.manager,{{"action","exportReport"},{"path",reportPath.u8string()}});
    std::ifstream reportFile(reportPath);json report;reportFile>>report;
    check(!report.contains("accountContext") && !report.contains("context") && !report.contains("signature") &&
              !report.contains("currentSaves") && !report.at("saveLocation").contains("path"),
          "Shared reports omit installation context, live-save IDs/signatures and private save paths");
    check(report.dump().find(source.id)==std::string::npos && report.dump().find(source.context)==std::string::npos,
          "Opaque current-save identity/context never leak indirectly into exported reports");
}
}
int main() {
    try {currentReadOnlyRoute();nativeWorkshopSaveProtection();actualWorkshopExportsRespectSaveRoots();portableProfileBackupHooks();independentSaveManagement();stableCliSaveSelection();stableCliDeletionReview();individualImportExport();individualReplacementDriftAndRecovery();transactionalDeletionRecovery();
#ifndef _WIN32
        deletionLateNameReplacement();
#endif
        std::cout<<"Current-save Manager tests passed "<<checks<<" checks.\n";return 0;}
    catch(const std::exception& issue){std::cerr<<"Current-save Manager tests failed after "<<checks<<" checks: "<<issue.what()<<'\n';return 1;}
}
