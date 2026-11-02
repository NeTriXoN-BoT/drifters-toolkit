#include "../src/minidump.hpp"
#include "minidump_fixture.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Synthetic minidumps only: no real crash dumps, files, OS APIs or network.
using drifter::minidump::MaxBytesRead;
using drifter::minidump::Source;
using drifter::minidump::summarize;
using drifter::minidump::text;
using json = nlohmann::json;
using namespace minidump_fixture;
namespace {
int checks = 0;
void require(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
struct Run {
    json summary;
    std::size_t reads = 0, bytes = 0;
};
Run run(const std::string& file) {
    Run result;
    Source source{file.size(), [&](std::uint64_t offset, char* data, std::size_t count) {
                      if (offset > file.size() || count > file.size() - offset)
                          throw std::runtime_error("parser read past the end of the source");
                      if (memoryEnd && offset < memoryEnd && offset + count > memoryStart)
                          throw std::runtime_error("parser read the private memory region");
                      ++result.reads;
                      result.bytes += count;
                      std::copy_n(file.data() + offset, count, data);
                  }};
    result.summary = summarize(source);
    return result;
}
bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}
bool lists(const json& summary, const std::string& name, const std::string& category) {
    for (const auto& module : summary["modules"]["listed"])
        if (module["name"] == name && module["category"] == category) return true;
    return false;
}
void noPrivateData(const json& summary, const std::string& label) {
    auto all = summary.dump() + text(summary);
    for (const char* secret :
         {"private-command-line", "private-user-name", "PRIVATE-MACHINE-ID", "PRIVATE-EPIC-ACCOUNT",
          "PRIVATE-PROCESS-MEMORY", "Private Person", "D:\\\\Games", "C:\\\\Tools"})
        require(!contains(all, secret), label + ": private value reached the summary: " + secret);
}

void completeAccessViolation() {
    auto file = build(defaultSpec());
    auto result = run(file);
    const auto& s = result.summary;
    require(s["status"] == "complete", "valid dump not complete");
    require(s["program"] == "Days Gone", "main executable not recognized");
    require(s["fingerprint"] == "c0000005:daysgone.exe+0x904178" && s["locationKey"] == s["fingerprint"],
            "fingerprint must be code plus module offset");
    require(s["exception"]["access"] == "read" && s["exception"]["nullAddress"] == true, "null read not decoded");
    require(s["location"]["category"] == "game" && s["location"]["offset"] == "0x904178", "location not decoded");
    require(s["secondsSinceStart"] == 56 && s["uptimeSource"] == "process",
            "Windows process start time must win over Unreal's own counter");
    require(s["crashedAt"] == 1792502768, "crash time missing");
    require(contains(s["headline"], "56 seconds after starting") && contains(s["headline"], "empty (null)") &&
                contains(s["headline"], "inside DaysGone.exe"),
            "headline not plain and specific: " + s["headline"].get<std::string>());
    require(s["system"]["windowsVersion"] == "10.0 build 26300" && s["system"]["logicalProcessors"] == 24 &&
                s["system"]["architecture"] == "x64",
            "system info not decoded");
    require(s["engine"]["version"] == "4.11.1-0+UE4" && s["hardware"]["gpu"] == "Fixture GPU" &&
                s["memory"]["outOfMemory"] == false && s["memory"]["usedPhysicalBytes"] == 4724464025ULL,
            "allowlisted crash-context fields missing");
    require(!s.contains("unreal"), "empty Unreal message fields must be omitted");
    require(s["modules"]["total"] == 9, "module count wrong");
    require(lists(s, "gameoverlayrenderer64.dll", "overlay") && lists(s, "dxgi.dll", "injected") &&
                lists(s,
                      "M\xc3\xb6"
                      "dHook.dll",
                      "third-party"),
            "overlay, proxy DLL or third-party module not listed (UTF-16 names must decode)");
    for (const char* hidden :
         {"DaysGone.exe", "PhysX3_x64.dll", "KERNELBASE.dll", "nvwgf2umx.dll", "MpOAV.dll", "steamclient64.dll"})
        for (const auto& module : s["modules"]["listed"])
            require(module["name"] != hidden, std::string("expected module listed as noise: ") + hidden);
    auto advice = s["advice"].dump();
    require(contains(advice, "within 56 seconds") && contains(advice, "dxgi.dll") &&
                contains(advice, "inside the game's own code"),
            "advice missing early-crash, proxy DLL or game-code guidance");
    noPrivateData(s, "complete dump");
    auto rendered = text(s);
    require(contains(rendered, "Fingerprint: c0000005:daysgone.exe+0x904178") &&
                contains(rendered, "Crashed at: 2026-10-20 13:26:08 UTC") &&
                contains(rendered, "Running time: 56 seconds") &&
                contains(rendered, "Memory in use: 4.4 GB of 48.0 GB (peak 5.0 GB)") &&
                contains(rendered, "without public debug symbols"),
            "text rendering incomplete:\n" + rendered);
    require(result.bytes < 64 * 1024, "summary read far more than the crash-record parts");
}

void everyTruncationIsSafe() {
    auto file = build(defaultSpec());
    for (std::size_t length = 0; length < file.size(); ++length) {
        auto result = run(file.substr(0, length));
        auto status = result.summary.value("status", "");
        require(status == "incomplete" || status == "complete" || status == "no-crash-record",
                "truncated dump produced an unknown state at length " + std::to_string(length));
        require(result.summary.contains("headline") && result.summary.contains("limits"),
                "truncated dump lost its explanation at length " + std::to_string(length));
        noPrivateData(result.summary, "truncation " + std::to_string(length));
        text(result.summary);
    }
}

void malformedInputs() {
    auto empty = run("");
    require(empty.summary["status"] == "incomplete" && contains(empty.summary["reason"], "empty") && !empty.reads,
            "empty file must be explained without reading");
    require(contains(text(empty.summary), "Reason: The dump file is empty"), "incomplete text lacks its reason");
    auto file = build(defaultSpec());
    auto wrong = file;
    wrong[0] = 'X';
    require(run(wrong).summary["status"] == "incomplete", "wrong signature accepted");
    auto version = file;
    version[4] = 1;
    require(run(version).summary["status"] == "incomplete", "unknown format version accepted");
    auto streams = file;
    set32(streams, 8, 0xFFFFFFFF);
    require(run(streams).summary["status"] == "incomplete", "huge stream count accepted");
    auto directory = file;
    set32(directory, 12, (std::uint32_t)file.size() - 4);
    require(run(directory).summary["status"] == "incomplete", "directory past the end accepted");
    auto textFile = std::string("This is a text file pretending to be a dump, long enough to pass size checks.");
    require(run(textFile).summary["status"] == "incomplete", "non-dump accepted");

    // Each directory entry's RVA/size checked: exception stream pointing outside the file.
    auto spec = defaultSpec();
    auto outside = build(spec);
    set32(outside, entryOf(outside, 6) + 8, 0xFFFFFF00);
    auto damaged = run(outside).summary;
    require(damaged["status"] == "no-crash-record" && damaged["damagedParts"] == true,
            "exception stream outside the file must be reported as damaged, not read");

    // A module count that would need gigabytes must not allocate or read them.
    auto modules = build(spec);
    set32(modules, get32(modules, entryOf(modules, 4) + 8), 0x7FFFFFFF);
    auto huge = run(modules);
    require(huge.summary["status"] == "complete" && huge.summary["damagedParts"] == true &&
                huge.summary["modules"]["total"] == 0 && huge.bytes < MaxBytesRead,
            "huge module count was trusted");
    require(huge.summary["location"]["category"] == "outside", "location claimed without a module list");

    // A module name length beyond the limit is skipped rather than read.
    auto names = build(spec);
    auto table = get32(names, entryOf(names, 4) + 8);
    set32(names, get32(names, table + 4 + 20), 0xFFFFFFF0); // first module's name length
    auto unnamed = run(names).summary;
    require(unnamed["status"] == "complete" && unnamed["location"]["module"] == "an unnamed module",
            "oversized module name was read");
}

void fatalErrors() {
    auto spec = defaultSpec();
    spec.code = 0x00000001;
    spec.address = 0x7ffa00000000ULL + 0xC408A;
    spec.parameters = {};
    spec.crashContext = defaultContext(
        "D:\\Build\\Unreal\\Game-X\\Engine\\Source\\Runtime\\Core\\Private\\GenericPlatform\\"
        "GenericPlatformMemory.cpp(152): LowLevelFatalError&nl;Ran out of memory allocating 18446744059095416832 "
        "bytes with alignment 65536&nl;",
        "1");
    auto s = run(build(spec)).summary;
    require(s["status"] == "complete" && s["exception"]["name"] == "Fatal error reported by the game",
            "Unreal fatal error not recognized");
    require(s["locationKey"] == "00000001:kernelbase.dll+0xc408a" &&
                s["fingerprint"].get<std::string>().rfind("00000001:message-", 0) == 0,
            "fatal errors must be told apart by message, paired by location");
    require(contains(s["headline"], "where errors are raised") && contains(s["headline"], "impossible amount"),
            "fatal-error headline misleading: " + s["headline"].get<std::string>());
    require(s["impossibleAllocation"] == true && contains(s["advice"].dump(), "damaged or mismatched data") &&
                !contains(s["advice"].dump(), "Close other programs"),
            "impossible allocation reported as an ordinary memory shortage");
    auto message = s["unreal"]["errorMessage"].get<std::string>();
    require(message.rfind("Engine\\Source\\Runtime\\Core", 0) == 0 && contains(message, "\nRan out of memory") &&
                !contains(message, "D:\\Build"),
            "engine message not made engine-relative with decoded newlines: " + message);
    auto other = spec;
    other.crashContext = defaultContext(
        "D:\\Build\\Unreal\\Game-X\\Engine\\Source\\Runtime\\Core\\Private\\GenericPlatform\\"
        "GenericPlatformMemory.cpp(152): LowLevelFatalError&nl;Ran out of memory allocating 18446744000000000000 "
        "bytes with alignment 65536&nl;",
        "1");
    require(run(build(other)).summary["fingerprint"] == s["fingerprint"],
            "a changing size must not split one fatal error into many fingerprints");
    auto real = spec;
    real.crashContext = defaultContext("", "1");
    auto oom = run(build(real)).summary;
    require(contains(oom["advice"].dump(), "Close other programs") && oom["locationKey"] == oom["fingerprint"],
            "real out-of-memory advice missing");
}

void otherShapes() {
    auto spec = defaultSpec();
    spec.parameters = {8, 0xFFFFFFFF};
    spec.address = 0xFFFFFFFF;
    auto outside = run(build(spec)).summary;
    require(outside["exception"]["access"] == "execute" && outside["location"]["category"] == "outside" &&
                outside["fingerprint"] == "c0000005:outside" && contains(outside["headline"], "damaged address"),
            "jump to an address outside every module not explained");
    spec = defaultSpec();
    spec.address = 0x7ffa10000000ULL + 0x1234;
    auto overlay = run(build(spec)).summary;
    require(overlay["location"]["category"] == "overlay" && contains(overlay["advice"].dump(), "Turn off this overlay"),
            "crash inside an overlay not flagged");
    spec = defaultSpec();
    spec.address = 0x7ffa30000000ULL + 0x10;
    auto driver = run(build(spec)).summary;
    require(driver["location"]["category"] == "graphics-driver" && contains(driver["advice"].dump(), "graphics driver"),
            "crash inside the graphics driver not flagged");
    spec = defaultSpec();
    spec.exception = false;
    auto snapshot = run(build(spec)).summary;
    require(snapshot["status"] == "no-crash-record" && contains(snapshot["headline"], "without a crash record"),
            "dump without an exception record not explained");
    require(contains(snapshot["advice"].dump(), "froze"), "a dump without a crash record has nothing to try");
    // Every crash type comes with something to try, including codes the summary does not know.
    for (std::uint32_t code : {0xC0000005u, 0xC0000006u, 0xC000001Du, 0xC0000094u, 0xC0000096u, 0xC00000FDu,
                               0xC0000017u, 0xC000012Du, 0xC0000135u, 0xC0000142u, 0xC0000374u, 0xC0000409u,
                               0xC0000602u, 0x80000003u, 0xE06D7363u, 0x00000001u, 0x12345678u}) {
        spec = defaultSpec();
        spec.code = code;
        spec.address = 0x10;
        auto typed = run(build(spec)).summary;
        require(typed["advice"].is_array() && !typed["advice"].empty(),
                "crash code without anything to try: " + std::to_string(code));
    }
    spec = defaultSpec();
    spec.misc = false;
    spec.utf16Context = true;
    auto unicode = run(build(spec)).summary;
    require(unicode["secondsSinceStart"] == 99 && unicode["uptimeSource"] == "unreal" &&
                unicode["engine"]["version"] == "4.11.1-0+UE4",
            "UTF-16 crash context not decoded");
    noPrivateData(unicode, "UTF-16 context");
    spec = defaultSpec();
    spec.crashContext = std::string(drifter::minidump::MaxCrashContextBytes + 1, 'x');
    auto oversized = run(build(spec)).summary;
    require(oversized["status"] == "complete" && oversized["damagedParts"] == true && !oversized.contains("engine"),
            "oversized crash context was read");
    spec = defaultSpec();
    spec.crashContext = "<NotAnUnrealContext><SecondsSinceStart>5</SecondsSinceStart></NotAnUnrealContext>";
    spec.misc = false;
    auto foreign = run(build(spec)).summary;
    require(!foreign.contains("secondsSinceStart"), "user stream without an Unreal crash context was trusted");
    spec = defaultSpec();
    spec.modules.clear();
    for (int i = 0; i < 1100; ++i)
        spec.modules.push_back(
            {0x100000000ULL + (std::uint64_t)i * 0x100000, 0x1000, std::u16string(500, u'a') + u"\\x.dll"});
    spec.address = 0x100000000ULL + 0x10;
    auto many = run(build(spec));
    require(many.bytes <= MaxBytesRead && many.summary["modules"]["truncated"] == true &&
                many.summary["modules"]["total"] == 1024 && many.summary["modules"]["listed"].size() <= 64,
            "large module list exceeded its read or report bounds");
}
}

int main() {
    try {
        completeAccessViolation();
        everyTruncationIsSafe();
        malformedInputs();
        fatalErrors();
        otherShapes();
        std::cout << "All " << checks << " crash dump summary checks passed. Synthetic dumps only.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Crash dump summary test failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
