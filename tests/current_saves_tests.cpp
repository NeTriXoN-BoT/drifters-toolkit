#ifndef DRIFTER_CORE_TESTING
#error Current-save regression hooks must stay test-only.
#endif
#include "../src/current_saves.hpp"
#include "../src/save_backups.hpp"
#include "../src/core_testing.hpp"
#include "../src/platform.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifndef _WIN32
#include <sys/stat.h>
#endif
using namespace drifter;
namespace drifter {
std::function<void(const char*)> currentSaveCheckpoint;
std::optional<std::uint64_t> currentSaveMillis;
void coreTestingCheckpoint(const char* name) { if(currentSaveCheckpoint) currentSaveCheckpoint(name); }
std::optional<std::uint64_t> coreTestingNowMillis() { return currentSaveMillis; }
}
namespace {
unsigned checks=0, skippedLinks=0;
void check(bool good,const char* reason) { ++checks;if(!good) throw std::runtime_error(reason); }
void write(const fs::path& file,const std::string& bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file,std::ios::binary);out<<bytes;
    if(!out) throw std::runtime_error("Fixture write failed");
}
template<class F> void refused(F action,const char* reason) {
    bool denied=false;try {action();}catch(const std::exception&) {denied=true;}check(denied,reason);
}
struct Fixture {
    fs::path root,saved;
    std::unique_ptr<CurrentSaves> service;
    Fixture() {
        root=fs::temp_directory_path()/("drifter-current-save-tests-"+std::to_string(GetCurrentProcessId())+"-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        check(!fs::exists(root),"Fixture must be fresh");
        saved=root/"Saved";fs::create_directories(saved/"SaveGames");
        service=std::make_unique<CurrentSaves>([this]{return save_locations::discover(saved);});
    }
    ~Fixture() {currentSaveCheckpoint={};currentSaveMillis.reset();service.reset();std::error_code error;fs::remove_all(root,error);}
    fs::path saves() const {return saved/"SaveGames";}
    json row(const json& list,const std::string& name) const {
        for(const auto& item:list.at("saves")) if(item.at("name")==name)return item;
        throw std::runtime_error("Missing current save fixture row");
    }
};
void inventoryAndSource() {
    Fixture f;
    auto empty=f.service->list();
    check(empty.at("status")=="ready" && empty.at("saves").empty(),"Empty actual save folder is ready without a backup");
    check(empty==f.service->list(),"Unchanged inventory signature is stable");
    write(f.saves()/"slot01.sav","exact native save bytes");
    write(f.saves()/"autosave01.SAV","second slot");
    write(f.saves()/"notes.txt","not a game save");
    write(f.saves()/"empty.sav","");
    fs::create_directory(f.saves()/"folder.sav");
    unsigned reads=0;
    currentSaveCheckpoint=[&](const char* name){if(std::string(name)=="current-save-read")++reads;};
    const auto list=f.service->list();
    check(reads==0,"Inventory reads native attributes without reading save contents");
    check(list.at("saves").size()==3,"Inventory includes direct .sav files only, including case-insensitive extension");
    check(list.at("skipped")==1,"A directory disguised as .sav is excluded");
    check(list.at("signature")!=empty.at("signature"),"Inventory change updates signature");
    check(list==f.service->list(),"Unchanged repeated inventory is stable");
    const auto row=f.row(list,"slot01.sav");
    check(row.at("size")==23,"Save byte size comes from native attributes");
    check(row.at("previewEligible")==true && f.row(list,"empty.sav").at("previewEligible")==false,"Only bounded non-empty saves are preview eligible");
    check(row.at("modifiedUtc").get<std::string>().find('T')!=std::string::npos,"Save time is UTC ISO8601");
    check(row.at("id").get<std::string>().size()==64 && list.at("context").get<std::string>().size()==64,"Opaque identifiers have bounded representation");
    check(list.dump().find(f.root.u8string())==std::string::npos,"Inventory does not expose private absolute root paths");
    const auto source=f.service->frozenSource(row.at("id"));
    check(source.id==row.at("id") && source.context==list.at("context"),"Frozen source carries its exact verified identity and context");
    check(std::string(source.bytes.begin(),source.bytes.end())=="exact native save bytes","Preview input is the selected actual save, without backup/copy mutation");
    check(reads>0,"Only explicit preview reads file contents");
    refused([&]{f.service->frozenSource(f.saves().u8string());},"A caller path cannot replace an opaque ID");
    refused([&]{f.service->frozenSource(std::string(64,'0'));},"An invented well-formed ID cannot authorize a read");
    refused([&]{f.service->frozenSource(f.row(list,"empty.sav").at("id"));},"Empty preview is refused");
    CurrentSaves other([&]{return save_locations::discover(f.saved);});
    const auto otherList=other.list();
    check(otherList.at("context")!=list.at("context") && f.row(otherList,"slot01.sav").at("id")!=row.at("id"),"Separate services use distinct nonce/key-bound identities");
    check(f.row(otherList,"slot01.sav").at("cliId")==row.at("cliId") && row.at("cliId").get<std::string>().size()==64,
          "The same exact native save has a stable bounded CLI lookup alias across services");
    refused([&]{other.frozenSource(row.at("cliId"));},"A stable CLI alias cannot authorize a native GUI source read");
    check(!fs::exists(f.root/"state") && !fs::exists(f.saves()/"manifest.json"),"Inventory/preview does not create state or backups in the game folder");
}
void immutableInspectionReader() {
    Fixture f;
    const std::string contents(8 * 1024 * 1024 + 1, 'S');
    write(f.saves()/"slot01.sav", contents);
    unsigned stoppedCalls = 0;
    CurrentSaves service([&]{return save_locations::discover(f.saved);}, [&] {
        ++stoppedCalls; throw std::runtime_error("Fixture game is running.");
    });
    const auto list = service.list(), row = f.row(list, "slot01.sav");
    const auto id = row.at("id").get<std::string>();
    check(!row.at("previewEligible") && row.at("exportEligible"), "Inspection uses the bounded80MiB export-read envelope, not the8MiB picture limit.");
    refused([&]{service.frozenSource(id);}, "Picture read did not retain its smaller decode budget.");
    const auto source = service.frozenInspectionSource(id);
    check(source.id == id && source.context == list.at("context") && source.bytes.size() == contents.size() &&
          std::equal(source.bytes.begin(), source.bytes.end(), contents.begin()),
          "Inspection reader did not return its exact identity-checked current bytes.");
    check(stoppedCalls == 0 && !fs::exists(f.root/"state"), "Read-only inspection observed game state or created replacement data.");
    refused([&]{service.frozenEditorSource(id);}, "Running-game editing bypassed its independent stopped-game guard.");
    check(stoppedCalls == 1, "An editing read did not call its native stopped-game guard.");
    refused([&]{service.frozenInspectionSource(std::string(64, '0'));}, "Inspection accepted an invented source capability.");
    bool changed = false;
    currentSaveCheckpoint = [&](const char* phase) {
        if (!changed && std::string(phase) == "current-save-after-read") {
            changed = true;
            write(f.saves()/"slot01.sav", "changed while inspecting");
        }
    };
    refused([&]{service.frozenInspectionSource(id);}, "An inspection admitted a save replaced while it was being frozen.");
    check(stoppedCalls == 1 && fs::file_size(f.saves()/"slot01.sav") == 24,
          "A failed inspection changed a current save or entered editing guards.");
}
void reviewedDeletion() {
    Fixture f;
    const auto selected=f.saves()/"slot01.sav",other=f.saves()/"slot02.sav";
    write(selected,"selected current save");write(other,"other save stays");
    bool running=false,failBackup=false;
    unsigned backups=0;
    auto stopped = [&]{if(running)throw std::runtime_error("fixture game running");};
    SaveBackups writer(f.root/"state", f.saves(), stopped);
    currentSaveCheckpoint = [&](const char* phase) { if (std::string(phase)=="save-backup-committed") ++backups; };
    CurrentSaves service([&]{return save_locations::discover(f.saved);}, stopped, {},
        [&](const CurrentSaveReplacement& deletion) {
            if(failBackup)throw std::runtime_error("fixture backup failure");
            return writer.deleteCurrent(deletion);
        });
    auto list=service.list();
    check(f.row(list,"slot01.sav")["deleteEligible"]==true,"Native callbacks make current-save deletion available.");
    const auto id=f.row(list,"slot01.sav")["id"].get<std::string>();
    const auto plan=service.reviewDeletion(id);
    check(plan["operation"]=="delete-current-save" && plan["safetyBackupRequired"]==true &&
          plan.dump().find(f.root.u8string())==std::string::npos,"Delete review describes backup-first effects without exposing caller paths.");
    refused([&]{service.deleteReviewed(plan["token"],false);},"A save delete needs explicit confirmation.");
    check(backups==0 && fs::exists(selected),"An unconfirmed delete never backs up or removes a save.");
    running=true;
    refused([&]{service.deleteReviewed(plan["token"],true);},"A game which starts after review blocks deletion.");
    check(backups==0 && fs::exists(selected),"Running-game refusal preserves all saves.");
    running=false;failBackup=true;
    const auto failed=service.reviewDeletion(id);
    refused([&]{service.deleteReviewed(failed["token"],true);},"A failed required backup blocks deletion.");
    check(fs::exists(selected) && backups==0,"A failed backup cannot delete its original save.");
    failBackup=false;
    const auto success=service.reviewDeletion(id);
    const auto removed=service.deleteReviewed(success["token"],true);
    check(removed["deleted"]==true && removed["safetyBackupId"].get<std::string>().size()==64 && backups==1,
          "Confirmed deletion returns its mandatory safety backup for the existing Restore flow.");
    const auto safety=f.root/"state/save-backups/entries"/removed["safetyBackupId"].get<std::string>()/"files";
    check(!fs::exists(selected) && fs::exists(other) && fs::file_size(safety/"slot01.sav")==21 &&
          fs::file_size(safety/"slot02.sav")==16,"Delete removes only one file after a complete independent backup.");
    refused([&]{service.deleteReviewed(success["token"],true);},"A used delete token cannot be replayed.");
}
void deletionDriftAndExpiry() {
    Fixture f;write(f.saves()/"slot01.sav","current save");
    unsigned backups=0;
    SaveBackups writer(f.root/"state",f.saves(),[]{});
    CurrentSaves service([&]{return save_locations::discover(f.saved);},[]{},{},
        [&](const CurrentSaveReplacement& deletion) { return writer.deleteCurrent(deletion); });
    auto id=f.row(service.list(),"slot01.sav")["id"].get<std::string>();
    auto plan=service.reviewDeletion(id);
    write(f.saves()/"slot01.sav","changed after review");
    refused([&]{service.deleteReviewed(plan["token"],true);},"A changed selected save invalidates its delete review.");
    check(backups==0 && fs::file_size(f.saves()/"slot01.sav")==20,"Changed files stay untouched before backup/deletion.");
    id=f.row(service.list(),"slot01.sav")["id"].get<std::string>();
    plan=service.reviewDeletion(id);
    currentSaveCheckpoint=[&](const char* phase){if(std::string(phase)=="save-backup-committed")++backups;
        if(std::string(phase)=="current-save-delete-after-backup")write(f.saves()/"slot01.sav","late changed save");};
    refused([&]{service.deleteReviewed(plan["token"],true);},"A save changed after the safety backup is preserved.");
    check(backups==1 && fs::file_size(f.saves()/"slot01.sav")==17,"Late-change refusal keeps the new current bytes.");
    currentSaveCheckpoint={};currentSaveMillis=1000;
    id=f.row(service.list(),"slot01.sav")["id"].get<std::string>();
    plan=service.reviewDeletion(id);
    currentSaveMillis=1000+10*60*1000+1;
    refused([&]{service.deleteReviewed(plan["token"],true);},"Save deletion review expires after ten minutes.");
    check(backups==1 && fs::exists(f.saves()/"slot01.sav"),"Expired review never starts a safety backup or deletion.");
}
void staleFilesAndRoots() {
    Fixture f;write(f.saves()/"slot01.sav","original");
    const auto original=f.service->list();const auto id=f.row(original,"slot01.sav").at("id").get<std::string>();
    write(f.saves()/"slot01.sav","different bytes");
    refused([&]{f.service->frozenSource(id);},"Changed size/time cannot reuse the previous ID");
    const auto changed=f.service->list();
    check(f.row(changed,"slot01.sav").at("id")!=id && changed.at("signature")!=original.at("signature"),"Changed save gets a new ID and inventory signature");
    check(f.row(changed,"slot01.sav").at("cliId")!=f.row(original,"slot01.sav").at("cliId"),
          "Changed save identity invalidates its previous CLI alias");
    const auto next=f.row(changed,"slot01.sav").at("id").get<std::string>();
    const auto timestamp=fs::last_write_time(f.saves()/"slot01.sav");
    fs::rename(f.saves()/"slot01.sav",f.saves()/"kept.bin");write(f.saves()/"slot01.sav","replacement 123");fs::last_write_time(f.saves()/"slot01.sav",timestamp);
    refused([&]{f.service->frozenSource(next);},"Replacing a file at the same name/time cannot reuse its token");
    const auto beforeRoot=f.service->list();const auto rootId=f.row(beforeRoot,"slot01.sav").at("id").get<std::string>();
    fs::rename(f.saves(),f.saved/"old-games");write(f.saves()/"slot01.sav","different root");
    refused([&]{f.service->frozenSource(rootId);},"Replacing the root directory invalidates all previous file IDs");
    check(f.service->list().at("context")!=beforeRoot.at("context"),"Root native identity changes the opaque context");
    check(f.row(f.service->list(),"slot01.sav").at("cliId")!=f.row(beforeRoot,"slot01.sav").at("cliId"),
          "A replacement root directory invalidates the CLI alias too");
    const auto active=f.service->list();const auto activeId=f.row(active,"slot01.sav").at("id").get<std::string>();
    write(f.saved/"76561198000000000/SaveGames/slot02.sav","another account");
    const auto ambiguous=f.service->list();
    check(ambiguous.at("status")=="ambiguous" && ambiguous.at("saves").empty(),"Ambiguous game accounts produce no guessed save inventory");
    refused([&]{f.service->frozenSource(activeId);},"Ambiguous account invalidates preview IDs");
    fs::remove_all(f.saves());fs::remove_all(f.saved/"old-games");
    const auto account=f.service->list();
    check(account.at("status")=="ready" && f.row(account,"slot02.sav").at("name")=="slot02.sav","A newly resolved account can be browsed read-only without a toolkit restart");
    check(f.row(account,"slot02.sav").at("cliId")!=f.row(active,"slot01.sav").at("cliId"),
          "CLI aliases cannot carry a selection into a different resolved game account");
}
void readRaces() {
    Fixture f;write(f.saves()/"slot01.sav",std::string(200000,'a'));
    const auto list=f.service->list();const auto id=f.row(list,"slot01.sav").at("id").get<std::string>();
    bool altered=false;
    currentSaveCheckpoint=[&](const char* phase){if(!altered && std::string(phase)=="current-save-before-read") {altered=true;write(f.saves()/"slot01.sav","short");}};
    refused([&]{f.service->frozenSource(id);},"A truncate/write while reading cannot return partially frozen save bytes");
    currentSaveCheckpoint={};write(f.saves()/"slot01.sav","current stable save");
    const auto stable=f.service->list();const auto stableId=f.row(stable,"slot01.sav").at("id").get<std::string>();
    currentSaveCheckpoint=[&](const char* phase){if(std::string(phase)=="current-save-after-read") {
        currentSaveCheckpoint={};fs::rename(f.saves()/"slot01.sav",f.saves()/"old.bin");write(f.saves()/"slot01.sav","replacement save");
    }};
    refused([&]{f.service->frozenSource(stableId);},"A named-file replacement after reading is rejected before decoder access");
    currentSaveCheckpoint={};
    const auto fresh=f.service->list();const auto freshId=f.row(fresh,"slot01.sav").at("id").get<std::string>();
    currentSaveCheckpoint=[&](const char* phase){if(std::string(phase)=="current-save-after-read") {
        currentSaveCheckpoint={};write(f.saved/"76561198000000000/SaveGames/account.sav","other account");
    }};
    refused([&]{f.service->frozenSource(freshId);},"A resolver/account change during the read is rejected");
}
void linksAndBounds() {
    Fixture f;write(f.saves()/"slot01.sav","safe file");
    std::error_code error;
    fs::create_symlink(f.saves()/"slot01.sav",f.saves()/"link.sav",error);
    if(error)++skippedLinks;else check(f.service->list().at("saves").size()==1,"Symlink saves are excluded");
    error.clear();fs::create_hard_link(f.saves()/"slot01.sav",f.saves()/"hardlink.sav",error);
    if(error)++skippedLinks;else {
        check(f.service->list().at("saves").empty(),"Hardlinked saves and their aliases are excluded");fs::remove(f.saves()/"hardlink.sav");
    }
#ifndef _WIN32
    check(mkfifo((f.saves()/"pipe.sav").c_str(),0600)==0,"FIFO fixture created");
    check(f.service->list().at("saves").size()==1,"FIFO inspection is nonblocking and excluded");
#endif
    for(unsigned i=0;i<140;++i)write(f.saves()/("slot-"+std::to_string(i)+".sav"),"slot");
    const auto bounded=f.service->list();
    check(bounded.at("saves").size()==128 && bounded.at("limited")==true,"Live inventory stays at its128-row bound");
    fs::remove_all(f.saves());fs::create_directory(f.saves());
    {std::ofstream sparse(f.saves()/"large.sav",std::ios::binary);sparse.seekp(8*1024*1024);sparse.put('x');}
    const auto large=f.service->list();
    check(f.row(large,"large.sav").at("previewEligible")==false,"Large saves remain listed but are not decoded");
    refused([&]{f.service->frozenSource(f.row(large,"large.sav").at("id"));},"Oversize save bytes are refused before reading/allocation");
    fs::remove(f.saves()/"large.sav");write(f.saves()/"slot01.sav","bounded slot");
    currentSaveMillis=1000;const auto timed=f.service->list();const auto timedId=f.row(timed,"slot01.sav").at("id").get<std::string>();
    currentSaveMillis=1000+10*60*1000+1;
    refused([&]{f.service->frozenSource(timedId);},"An idle preview token expires");
    currentSaveMillis.reset();
    fs::remove_all(f.saves());const auto missing=f.service->list();
    check(missing.at("status")=="missing" && missing.at("saves").empty(),"Missing saves return a quiet empty status");
    check(!fs::exists(f.saves()),"Polling does not create missing game save folders");
}
void nativeIndividualExport() {
    Fixture f; write(f.saves()/"slot01.sav", "frozen selected progress"); write(f.saves()/"slot02.sav", "other live progress");
    const auto listed=f.service->list(), id=f.row(listed,"slot01.sav").at("id");
    check(f.row(listed,"slot01.sav")["exportEligible"]==true && f.row(listed,"slot01.sav")["replaceEligible"]==false,
          "Read-only inventory exposed replacement without a native writer or hid a bounded export");
    const auto destination=f.root/"exports/copy.sav"; fs::create_directories(destination.parent_path());
    const auto result=f.service->exportSelected(id,destination);
    std::ifstream output(destination,std::ios::binary);
    check(result["exported"]==true && std::string(std::istreambuf_iterator<char>(output),{})=="frozen selected progress",
          "Native export did not preserve the selected source bytes"); output.close();
    refused([&]{f.service->exportSelected(id,destination);},"Native export overwrote an existing output");
    refused([&]{f.service->exportSelected(id,f.saves()/"othername.sav");},"Export created a new live save without an import review");
    fs::create_directories(f.saves()/"nested");
    refused([&]{f.service->exportSelected(id,f.saves()/"nested/copy.sav");},"Export wrote under a physical source-root descendant");
    refused([&]{f.service->exportSelected(id,destination.parent_path()/"invalid.txt");},"Export accepted a non-save destination name");
    const auto alias=f.root/"save-alias"; std::error_code error; fs::create_directory_symlink(f.saves(),alias,error);
    if(!error)refused([&]{f.service->exportSelected(id,alias/"copy.sav");},"An alias bypassed native live-root export protection");
    write(f.saves()/"slot01.sav","newer progress after inventory");
    refused([&]{f.service->exportSelected(id,destination.parent_path()/"stale.sav");},"Export used a stale source selection");
    check(!fs::exists(destination.parent_path()/"stale.sav") && fs::exists(f.saves()/"slot02.sav"),"A refused export left an output or changed another live save");
}
}
int main() {
    try {inventoryAndSource();immutableInspectionReader();staleFilesAndRoots();readRaces();linksAndBounds();reviewedDeletion();deletionDriftAndExpiry();nativeIndividualExport();
        std::cout<<"Current-save tests passed "<<checks<<" checks; "<<skippedLinks<<" link fixtures unavailable.\n";return 0;
    }catch(const std::exception& issue) {std::cerr<<"Current-save tests failed after "<<checks<<" checks: "<<issue.what()<<'\n';return 1;}
}
