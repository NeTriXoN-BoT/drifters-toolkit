#include "../src/core.hpp"
#include "../src/core_testing.hpp"
#include "../src/platform.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace drifter;
namespace {
unsigned checks=0;
void check(bool good,const char* reason) {++checks;if(!good)throw std::runtime_error(reason);}
void write(const fs::path& path,const std::string& bytes) {
    fs::create_directories(path.parent_path());std::ofstream output(path,std::ios::binary);output<<bytes;
    if(!output)throw std::runtime_error("Fixture write failed");
}
struct Fixture {
    fs::path root,state,active;
    CoreTestingHooks hooks;
    std::unique_ptr<Manager> manager;
    std::map<std::string,unsigned> reads;
    std::size_t budget=600,resident=0;
    Fixture() {
        root=fs::temp_directory_path()/("drifter-index-budget-"+std::to_string(GetCurrentProcessId())+"-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        check(!fs::exists(root),"Private resource fixture root is new");
        state=root/"state";active=state/"Saved/Paks";fs::create_directories(active);
        write(root/"app/catalog/catalog.json",R"({"schemaVersion":1,"mods":[]})");
        hooks.gameRunning=[] {return std::optional<bool>(false);};
        hooks.installedIndexBudgetBytes=budget;
        hooks.installedIndexAssets=[this](const fs::path& path)->std::optional<std::vector<std::string>> {
            ++reads[path.filename().u8string()];
            // Large retained capacity with a small logical asset name models
            // normalized paths whose long mount prefix has been stripped.
            std::string asset(250,'x');asset.resize(1);asset[0]=path.filename().u8string()[0];
            std::vector<std::string> assets;assets.push_back(std::move(asset));return assets;
        };
        hooks.installedIndexResidentBytes=[this](std::size_t bytes) {resident=bytes;};
        setCoreTestingHooks(&hooks);
        manager=std::make_unique<Manager>(root/"app",state,active,ManagerOptions{RecoveryMode::Deferred});
        write(active/"a.pak","first actual file bytes");write(active/"b.pak","second actual file bytes");write(active/"c.pak","third actual file bytes");
    }
    ~Fixture(){manager.reset();setCoreTestingHooks(nullptr);std::error_code error;fs::remove_all(root,error);}
    json snapshot(bool force=false) {
        auto response=manager->handle({{"action",force?"scan":"snapshot"}});
        check(response.value("ok",false),"Budgeted native scan returns an honest snapshot");
        check(resident<=budget,"Rejected indexes cannot keep vector/string allocations beyond the resident budget");
        return response.at("data");
    }
    unsigned count() const {unsigned total=0;for(const auto& row:reads)total+=row.second;return total;}
    json unknown(const json& snapshot) const {
        for(const auto& row:snapshot.at("installed"))if(!row.value("readable",false))return row;
        throw std::runtime_error("Expected an over-budget installed entry");
    }
};
void resourceBudget() {
    Fixture f;
    auto first=f.snapshot();
    check(f.count()==3,"Each new real file gets one initial index read");
    auto rejected=f.unknown(first);
    check(!rejected.value("readable",true) && !rejected.contains("assetCount") && !rejected.value("owned",true) &&
              !rejected.value("authorVerified",true),"Rejected metadata never becomes compatible assets, ownership or author trust");
    check(rejected.at("error").get<std::string>().find("memory limit")!=std::string::npos,"Resource-limited compatibility is explained");
    const auto before=f.count();f.snapshot();
    check(f.count()==before,"Unchanged over-budget snapshots do not repeatedly parse a rejected index");
    std::string removed;
    for(const auto& row:first.at("installed"))if(row.value("readable",false)) {removed=row.at("file").get<std::string>();break;}
    check(!removed.empty(),"At least one readable copy exists below the bound");
    fs::remove(f.active/fs::u8path(removed));
    auto smaller=f.snapshot();
    check(f.count()==before+1,"Freed capacity reloads the formerly rejected real index");
    check(std::all_of(smaller.at("installed").begin(),smaller.at("installed").end(),[](const json& row){return row.value("readable",false);}),
          "Reducing the collection makes the complete remaining indexes usable");
    write(f.active/fs::u8path(removed),"replacement actual file bytes");
    const auto crowded=f.snapshot();rejected=f.unknown(crowded);
    const auto beforeChange=f.count();
    write(f.active/fs::u8path(rejected.at("file").get<std::string>()),"a different-sized changed file");
    f.snapshot();check(f.count()==beforeChange+1,"Changed rejected file is re-verified despite the remembered budget result");
    const auto beforeForce=f.count();f.snapshot(true);
    check(f.count()==beforeForce+3,"An explicit scan re-reads every package and cannot trust a metadata-only budget decision");
    check(!fs::exists(f.state/"state.json"),"Resource handling never adopts or rewrites installed package state");
}
}
int main() {
    try {resourceBudget();std::cout<<"Index-cache budget tests passed "<<checks<<" checks.\n";return 0;}
    catch(const std::exception& issue){std::cerr<<"Index-cache budget tests failed after "<<checks<<" checks: "<<issue.what()<<'\n';return 1;}
}
