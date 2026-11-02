#include "minidump.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace drifter::minidump {
using json = nlohmann::json;
namespace {
// Format problems become an "incomplete" summary. Source read errors are not
// caught here: they are real I/O failures the caller must report.
struct Malformed : std::runtime_error {
    using std::runtime_error::runtime_error;
};
[[noreturn]] void malformed(const char* why) {
    throw Malformed(why);
}

constexpr std::uint32_t Signature = 0x504D444D; // "MDMP"
constexpr std::uint16_t Version = 0xA793;
constexpr std::uint32_t ModuleListStream = 4, ExceptionStream = 6, SystemInfoStream = 7, MiscInfoStream = 15;
// Unreal Engine 4 writes its crash context XML as the first user stream
// (LastReservedStream + 1). Its content is checked before any field is used.
constexpr std::uint32_t UnrealCrashContextStream = 0x10000;
constexpr std::size_t ModuleRecordBytes = 108, ExceptionStreamBytes = 168;

class Reader {
public:
    explicit Reader(const Source& source) : source_(source) {}
    std::string bytes(std::uint64_t offset, std::uint64_t count) {
        if (offset > source_.size || count > source_.size - offset)
            malformed("A part of the dump points past the end of the file.");
        if (count > MaxBytesRead - used_) malformed("The dump asks for more data than a summary may read.");
        used_ += (std::size_t)count;
        std::string data((std::size_t)count, '\0');
        if (count) source_.read(offset, data.data(), (std::size_t)count);
        return data;
    }

private:
    const Source& source_;
    std::size_t used_ = 0;
};

std::uint16_t u16(const std::string& b, std::size_t at) {
    if (at > b.size() || b.size() - at < 2) malformed("A dump record is shorter than its format requires.");
    return (std::uint16_t)((unsigned char)b[at] | ((unsigned)(unsigned char)b[at + 1] << 8));
}
std::uint32_t u32(const std::string& b, std::size_t at) {
    return (std::uint32_t)u16(b, at) | ((std::uint32_t)u16(b, at + 2) << 16);
}
std::uint64_t u64(const std::string& b, std::size_t at) {
    return (std::uint64_t)u32(b, at) | ((std::uint64_t)u32(b, at + 4) << 32);
}

void appendUtf8(std::string& out, std::uint32_t code) {
    if (code < 0x80)
        out += (char)code;
    else if (code < 0x800) {
        out += (char)(0xc0 | (code >> 6));
        out += (char)(0x80 | (code & 63));
    } else if (code < 0x10000) {
        out += (char)(0xe0 | (code >> 12));
        out += (char)(0x80 | ((code >> 6) & 63));
        out += (char)(0x80 | (code & 63));
    } else {
        out += (char)(0xf0 | (code >> 18));
        out += (char)(0x80 | ((code >> 12) & 63));
        out += (char)(0x80 | ((code >> 6) & 63));
        out += (char)(0x80 | (code & 63));
    }
}
// Control characters become spaces (newlines and tabs are kept only where a
// field may span lines). Output is always valid UTF-8.
std::uint32_t printable(std::uint32_t code, bool multiline) {
    if (code == '\n' || code == '\t') return multiline ? code : ' ';
    if (code < 0x20 || code == 0x7f || (code >= 0x80 && code < 0xa0)) return ' ';
    return code;
}
std::string fromUtf16(const std::string& bytes, bool multiline = false) {
    std::string out;
    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
        std::uint32_t unit = u16(bytes, i);
        if (unit >= 0xd800 && unit <= 0xdbff && i + 3 < bytes.size()) {
            auto next = u16(bytes, i + 2);
            if (next >= 0xdc00 && next <= 0xdfff) {
                unit = 0x10000 + ((unit - 0xd800) << 10) + (next - 0xdc00);
                i += 2;
            } else
                unit = 0xfffd;
        } else if (unit >= 0xd800 && unit <= 0xdfff)
            unit = 0xfffd;
        if (!unit) break;
        if (unit == '\r') continue;
        appendUtf8(out, printable(unit, multiline));
    }
    return out;
}
std::string fromUtf8(const std::string& bytes, bool multiline = false) {
    static const std::uint32_t minimum[] = {0, 0x80, 0x800, 0x10000};
    std::string out;
    for (std::size_t i = 0; i < bytes.size();) {
        auto first = (unsigned char)bytes[i++];
        if (!first || first == '\r') continue;
        if (first < 0x80) {
            appendUtf8(out, printable(first, multiline));
            continue;
        }
        unsigned count = 0;
        std::uint32_t code = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            count = 1;
            code = first & 31;
        } else if (first >= 0xe0 && first <= 0xef) {
            count = 2;
            code = first & 15;
        } else if (first >= 0xf0 && first <= 0xf4) {
            count = 3;
            code = first & 7;
        }
        bool valid = count && i + count <= bytes.size();
        for (unsigned n = 0; valid && n < count; ++n) {
            auto next = (unsigned char)bytes[i + n];
            valid = (next & 0xc0) == 0x80;
            code = (code << 6) | (next & 63);
        }
        if (!valid || code < minimum[count] || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) {
            appendUtf8(out, 0xfffd);
            continue;
        }
        i += count;
        appendUtf8(out, printable(code, multiline));
    }
    return out;
}
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return c < 0x80 ? (char)std::tolower(c) : (char)c; });
    return value;
}
std::string hex(std::uint64_t value, int width = 0) {
    char text[32];
    std::snprintf(text, sizeof(text), "%0*llX", width, (unsigned long long)value);
    return text;
}
std::string baseName(const std::string& path) {
    auto at = path.find_last_of("\\/");
    return at == std::string::npos ? path : path.substr(at + 1);
}
std::string trim(const std::string& value) {
    auto begin = value.find_first_not_of(" \t\n");
    if (begin == std::string::npos) return {};
    auto end = value.find_last_not_of(" \t\n");
    return value.substr(begin, end - begin + 1);
}
std::string normalizedPath(const std::string& path) {
    auto value = lower(path);
    std::replace(value.begin(), value.end(), '/', '\\');
    return value;
}

// ---------------------------------------------------------------- crash context
// Plain string search over the decoded XML. No XML parser, entity or DTD is
// processed, and only the allowlisted fields below are ever read.
std::string decodeEntities(std::string value) {
    // "&nl;" is Unreal's own newline marker inside crash context messages.
    static const std::pair<const char*, const char*> entities[] = {{"&nl;", "\n"},   {"&lt;", "<"},   {"&gt;", ">"},
                                                                   {"&quot;", "\""}, {"&apos;", "'"}, {"&amp;", "&"}};
    for (const auto& [from, to] : entities)
        for (std::size_t at = 0; (at = value.find(from, at)) != std::string::npos; at += std::strlen(to))
            value.replace(at, std::strlen(from), to);
    return value;
}
std::optional<std::string> tagValue(const std::string& xml, const std::string& name, std::size_t limit) {
    auto open = "<" + name + ">", close = "</" + name + ">";
    auto begin = xml.find(open);
    if (begin == std::string::npos) return std::nullopt;
    begin += open.size();
    auto end = xml.find(close, begin);
    if (end == std::string::npos) return std::nullopt;
    auto value = decodeEntities(xml.substr(begin, std::min(end - begin, limit + 64)));
    if (value.size() > limit) value = value.substr(0, limit) + " [shortened]";
    return value;
}
// Engine messages carry the developer's build paths, for example
// "D:\Build\Unreal\Engine\Source\Runtime\Core\...\File.cpp(152)". Keep the
// useful engine-relative part so later path redaction does not erase it.
std::string engineRelative(std::string text) {
    for (const char* marker : {"\\Engine\\Source\\", "\\Engine\\Plugins\\", "\\BendGame\\Source\\",
                               "\\BendGame\\Plugins\\", "/Engine/Source/", "/BendGame/Source/"}) {
        for (std::size_t at = 0; (at = text.find(marker, at)) != std::string::npos;) {
            auto start = at;
            while (start > 0 && !std::strchr(" \t\n(\"'<>", text[start - 1])) --start;
            if (at - start >= 2 && std::isalpha((unsigned char)text[start]) && text[start + 1] == ':') {
                text.erase(start, at + 1 - start);
                at = start + 1;
            } else
                at += std::strlen(marker);
        }
    }
    return text;
}
std::uint64_t fnv1a(const std::string& value) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char c : value) hash = (hash ^ c) * 1099511628211ULL;
    return hash;
}
// "Ran out of memory allocating N bytes" from Unreal's fatal error text.
std::optional<std::uint64_t> requestedAllocation(const std::string& message) {
    static const std::string marker = "allocating ";
    auto at = message.find(marker);
    if (at == std::string::npos) return std::nullopt;
    at += marker.size();
    std::uint64_t value = 0;
    auto parsed = std::from_chars(message.data() + at, message.data() + message.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr == message.data() + at) return std::nullopt;
    return value;
}
std::optional<std::uint64_t> number(const std::optional<std::string>& value) {
    if (!value) return std::nullopt;
    auto text = trim(*value);
    std::uint64_t result = 0;
    auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return std::nullopt;
    return result;
}

// ---------------------------------------------------------------- modules
struct Module {
    std::uint64_t base = 0, size = 0;
    std::string name, path; // the path stays internal; only the file name is reported
};
bool startsWithAny(const std::string& value, std::initializer_list<const char*> prefixes) {
    for (const auto* prefix : prefixes)
        if (value.rfind(prefix, 0) == 0) return true;
    return false;
}
// Categories: game, windows, graphics-driver, overlay, injected, platform, third-party.
std::string category(const Module& module, std::size_t index, const std::string& gameRoot) {
    auto name = lower(module.name), path = normalizedPath(module.path);
    if (index == 0) return "game";
    if (startsWithAny(name, {"nvwgf2um", "nvd3dum", "nvoglv", "nvldumd", "nvgpucomp", "atidxx", "amdxx", "atiumd",
                             "amdihk", "aticfx", "atio6axx", "igd10", "igd11", "igd12", "igxe", "igdumd", "igc64",
                             "ig9icd", "vulkan-1"}))
        return "graphics-driver";
    static const std::set<std::string> overlays = {"gameoverlayrenderer64.dll",
                                                   "gameoverlayrenderer.dll",
                                                   "discordhook64.dll",
                                                   "discordhook.dll",
                                                   "rtsshooks64.dll",
                                                   "rtsshooks.dll",
                                                   "overlay64.dll",
                                                   "nvspcap64.dll",
                                                   "igo64.dll",
                                                   "eosovh-win64-shipping.dll",
                                                   "owclient.dll",
                                                   "graphics-hook64.dll"};
    if (overlays.count(name)) return "overlay";
    static const std::set<std::string> proxies = {"dxgi.dll",      "d3d11.dll",   "d3d12.dll",    "d3d9.dll",
                                                  "dinput8.dll",   "version.dll", "winmm.dll",    "xinput1_3.dll",
                                                  "xinput1_4.dll", "dsound.dll",  "opengl32.dll", "winhttp.dll"};
    if (!gameRoot.empty() && path.rfind(gameRoot, 0) == 0) return proxies.count(name) ? "injected" : "game";
    if (path.size() > 11 && path[1] == ':' && path.compare(2, 9, "\\windows\\") == 0) return "windows";
    if (path.find("\\microsoft\\windows defender\\") != std::string::npos) return "windows";
    static const std::set<std::string> platform = {"steamclient64.dll",  "steamclient.dll",          "tier0_s64.dll",
                                                   "tier0_s.dll",        "vstdlib_s64.dll",          "vstdlib_s.dll",
                                                   "crashhandler64.dll", "crashhandler.dll",         "steam_api64.dll",
                                                   "galaxy64.dll",       "eossdk-win64-shipping.dll"};
    if (platform.count(name)) return "platform";
    return "third-party";
}
const char* categoryLabel(const std::string& value) {
    if (value == "game") return "part of the game";
    if (value == "windows") return "a Windows system file";
    if (value == "graphics-driver") return "the graphics driver";
    if (value == "overlay") return "an overlay program";
    if (value == "injected") return "a proxy DLL placed in the game folder";
    if (value == "platform") return "a launcher component";
    return "a program that is not part of the game";
}
bool runtimeLibrary(const std::string& name) {
    auto value = lower(name);
    return value.rfind("vcruntime140", 0) == 0 || value.rfind("msvcp140", 0) == 0 || value == "ucrtbase.dll";
}

// ---------------------------------------------------------------- exceptions
struct ExceptionInfo {
    const char* name;
    const char* explanation;
};
ExceptionInfo describe(std::uint32_t code) {
    switch (code) {
    case 0xC0000005: return {"Access violation", "It tried to use memory it may not use."};
    case 0xC0000006: return {"In-page error", "Windows could not load part of a file from disk into memory."};
    case 0xC000001D: return {"Illegal instruction", "The processor was asked to run an invalid instruction."};
    case 0xC0000094: return {"Integer division by zero", "The game divided a whole number by zero."};
    case 0xC0000096: return {"Privileged instruction", "The game tried to run an instruction reserved for Windows."};
    case 0xC00000FD: return {"Stack overflow", "A chain of function calls went too deep and ran out of stack space."};
    case 0xC0000017: return {"Out of memory", "Windows could not give the game the memory it asked for."};
    case 0xC000012D: return {"Commit limit reached", "Windows ran out of memory, including the page file."};
    case 0xC0000135: return {"Required DLL not found", "A file the game needs at startup was missing."};
    case 0xC0000142: return {"DLL initialization failed", "A program file failed while the game was starting."};
    case 0xC0000374: return {"Heap corruption", "The game's memory bookkeeping was damaged."};
    case 0xC0000409:
        return {"Fail-fast stop", "A safety check stopped the game immediately because its memory looked damaged."};
    case 0xC0000602: return {"Fail-fast stop", "A safety check stopped the game immediately."};
    case 0x80000003: return {"Breakpoint", "The game hit a debugging breakpoint it was not expected to reach."};
    case 0xE06D7363: return {"Unhandled C++ exception", "An internal error was raised and nothing handled it."};
    case 0x00000001:
        return {"Fatal error reported by the game",
                "Unreal Engine stopped the game on purpose after an internal fatal error."};
    default: return {"Unknown crash code", "Windows recorded a crash code this summary does not recognize."};
    }
}
// Codes whose recorded location is the function that raised them, not the cause.
bool raisedByWindows(std::uint32_t code) {
    return code == 0x00000001 || code == 0xE06D7363 || code == 0x80000003 || code == 0xC0000409 || code == 0xC0000602 ||
           code == 0xC0000374;
}
std::string duration(std::uint64_t seconds) {
    auto plural = [](std::uint64_t value, const char* unit) {
        return std::to_string(value) + " " + unit + (value == 1 ? "" : "s");
    };
    if (seconds < 90) return plural(seconds, "second");
    if (seconds < 90 * 60) return plural((seconds + 30) / 60, "minute");
    return plural((seconds + 1800) / 3600, "hour");
}
std::string gigabytes(std::uint64_t bytes) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    return text;
}
const char* const OutOfMemoryAdvice = "The game ran out of memory. Close other programs, keep the Windows page file "
                                      "enabled and test without large texture or model mods.";
// The fallback, so every summary has at least one thing to try.
const char* const GeneralAdvice =
    "Start the game with your mods switched off. If it no longer crashes, Find the bad mod "
    "narrows down which mod causes it. If it still crashes, verify the game files in "
    "Steam or Epic and update your graphics driver.";
const char* const HookedToolsAdvice = "Test without tools that hook into the game (ReShade, overlays, DLL mods) and "
                                      "without CPU, GPU or RAM overclocks, then add them back one at a time.";
// What to try for each crash type Windows records. Empty when the type needs nothing beyond the general rules.
std::string typeAdvice(std::uint32_t code, bool nullAddress) {
    switch (code) {
    case 0xC0000005:
        return nullAddress
                   ? "The game used something that was missing. In Days Gone this is most often a mod that does not "
                     "match your game version, or one that removes something the game still expects. Switch off "
                     "mods you added or updated recently, then test the same spot."
                   : "This kind of crash in Days Gone usually comes from a mod made for a different game version, a "
                     "tool that hooks into the game or an unstable overclock. Switch your mods off and test the same "
                     "spot; if the crash is gone, Find the bad mod finds the cause.";
    case 0xC000001D:
    case 0xC0000096:
        return "The processor was given an instruction it could not run. Remove CPU and GPU overclocks or undervolts "
               "while testing, update your graphics driver and BIOS, and verify the game files in Steam or Epic.";
    case 0xC0000094:
        return "A setting or a mod gave the game a value it could not use. Reset the graphics settings in the game's "
               "menu to their defaults and test without mods that change gameplay values.";
    case 0xC00000FD:
        return "The game got stuck in a loop. This usually means game data that does not fit together, such as two "
               "mods that change the same thing or a mod for another game version. Switch off mods you added "
               "recently and verify the game files.";
    case 0xC0000017:
    case 0xC000012D:
        return "Let Windows manage the page file size (System managed) and keep free space on the system drive.";
    case 0xC0000135:
        return "A file the game needs to start is missing. Verify the game files in Steam or Epic and install the "
               "latest Microsoft Visual C++ Redistributable (x64). If you use a DLL mod or ReShade, check that all "
               "of its files are in the game folder.";
    case 0xC0000142:
        return "Something that loads into the game failed while it started: a DLL mod, an overlay or security "
               "software blocking it. Move DLL mods out of the game folder, turn off overlays and verify the game "
               "files.";
    case 0xC0000374:
    case 0xC0000409:
    case 0xC0000602:
        return std::string("The game's memory was damaged before it stopped. Common causes are tools that hook into "
                           "the game, an unstable overclock or faulty RAM, and mods for another game version. ") +
               HookedToolsAdvice;
    case 0x80000003:
        return "The game hit a check it should never reach. This usually means a changed game file or a tool hooking "
               "into the game. Verify the game files and test without DLL mods and overlays.";
    case 0xE06D7363:
        return "An internal error had nothing to handle it. This is often a missing or damaged file, or a tool "
               "hooking into the game. Verify the game files and test without DLL mods and overlays.";
    case 0x00000001:
        return "Read the game's error message in this summary: it often names the file that failed. If it names a "
               "mod's file, update or remove that mod; otherwise verify the game files in Steam or Epic.";
    default: return "";
    }
}

void summarizeChecked(const Source& source, json& out) {
    Reader in(source);
    if (source.size == 0) malformed("The dump file is empty. The game probably stopped before it finished writing it.");
    if (source.size < 32) malformed("The file is too short to be a crash dump.");
    auto header = in.bytes(0, 32);
    if (u32(header, 0) != Signature) malformed("This file is not a Windows crash dump.");
    if (u16(header, 4) != Version) malformed("This crash dump uses an unknown format version.");
    auto streamCount = u32(header, 8), directory = u32(header, 12), timestamp = u32(header, 20);
    if (!streamCount || streamCount > MaxStreams) malformed("The dump's list of contents has an invalid size.");
    auto entries = in.bytes(directory, (std::uint64_t)streamCount * 12);
    if (timestamp) out["crashedAt"] = timestamp;

    struct Stream {
        std::uint32_t size = 0, rva = 0;
    };
    std::map<std::uint32_t, Stream> streams;
    bool damaged = false;
    for (std::uint32_t i = 0; i < streamCount; ++i) {
        auto type = u32(entries, i * 12), size = u32(entries, i * 12 + 4), rva = u32(entries, i * 12 + 8);
        if (type != ModuleListStream && type != ExceptionStream && type != SystemInfoStream && type != MiscInfoStream &&
            type != UnrealCrashContextStream)
            continue;
        if (streams.count(type)) continue;
        if ((std::uint64_t)rva + size > source.size) {
            damaged = true; // typically a dump that was cut off while being written
            continue;
        }
        streams[type] = {size, rva};
    }
    auto present = [&](std::uint32_t type, std::uint32_t minimum) -> const Stream* {
        auto found = streams.find(type);
        return found != streams.end() && found->second.size >= minimum ? &found->second : nullptr;
    };

    // Modules: only file names are reported; full paths are used to classify them.
    std::vector<Module> modules;
    bool modulesTruncated = false;
    if (auto stream = present(ModuleListStream, 4)) {
        try {
            auto count = u32(in.bytes(stream->rva, 4), 0);
            if (count > MaxModules) {
                count = MaxModules;
                modulesTruncated = true;
            }
            if (4 + (std::uint64_t)count * ModuleRecordBytes > stream->size)
                malformed("The dump's module list is shorter than it claims.");
            auto table = in.bytes((std::uint64_t)stream->rva + 4, (std::uint64_t)count * ModuleRecordBytes);
            for (std::uint32_t i = 0; i < count; ++i) {
                auto at = (std::size_t)i * ModuleRecordBytes;
                Module module;
                module.base = u64(table, at);
                module.size = u32(table, at + 8);
                auto nameRva = u32(table, at + 20);
                if ((std::uint64_t)nameRva + 4 <= source.size) {
                    auto length = u32(in.bytes(nameRva, 4), 0);
                    if (length <= MaxModuleNameBytes && (std::uint64_t)nameRva + 4 + length <= source.size) {
                        module.path = fromUtf16(in.bytes((std::uint64_t)nameRva + 4, length));
                        module.name = baseName(module.path);
                    }
                }
                modules.push_back(std::move(module));
            }
        } catch (const Malformed&) {
            damaged = true;
        }
    }
    std::string gameRoot;
    if (!modules.empty()) {
        auto path = normalizedPath(modules[0].path);
        auto bend = path.find("\\bendgame\\binaries\\");
        auto slash = path.find_last_of('\\');
        if (bend != std::string::npos)
            gameRoot = path.substr(0, bend + 1);
        else if (slash != std::string::npos)
            gameRoot = path.substr(0, slash + 1);
    }
    std::vector<std::string> categories;
    for (std::size_t i = 0; i < modules.size(); ++i) categories.push_back(category(modules[i], i, gameRoot));
    auto executable = modules.empty() ? std::string() : modules[0].name;
    std::string program = lower(executable) == "daysgone.exe" ? "Days Gone"
                          : executable.empty()                ? "The game"
                                                              : executable;
    out["program"] = program;

    if (auto stream = present(SystemInfoStream, 24)) {
        try {
            auto system = in.bytes(stream->rva, 24);
            auto arch = u16(system, 0);
            out["system"] = {{"windowsVersion", std::to_string(u32(system, 8)) + "." + std::to_string(u32(system, 12)) +
                                                    " build " + std::to_string(u32(system, 16))},
                             {"logicalProcessors", (unsigned)(unsigned char)system[6]},
                             {"architecture", arch == 9    ? "x64"
                                              : arch == 12 ? "ARM64"
                                              : arch == 0  ? "x86"
                                                           : "other"}};
        } catch (const Malformed&) {
            damaged = true;
        }
    }

    // Unreal crash context: allowlisted fields only. Account, machine,
    // command-line and folder fields are never looked up.
    std::optional<std::uint64_t> unrealSeconds;
    bool outOfMemory = false;
    if (auto stream = present(UnrealCrashContextStream, 16)) {
        try {
            if (stream->size > MaxCrashContextBytes) malformed("The game's crash context is larger than allowed.");
            auto raw = in.bytes(stream->rva, stream->size);
            bool bom = (unsigned char)raw[0] == 0xff && (unsigned char)raw[1] == 0xfe;
            auto xml = bom || raw[1] == '\0' ? fromUtf16(raw.substr(bom ? 2 : 0), true) : fromUtf8(raw, true);
            if (xml.find("<FGenericCrashContext") != std::string::npos) {
                json engine = json::object(), memory = json::object(), hardware = json::object(),
                     unreal = json::object();
                if (auto value = number(tagValue(xml, "SecondsSinceStart", 32)); value && *value < 100000000)
                    unrealSeconds = value;
                for (const auto& [tag, key, limit] : {std::tuple{"EngineVersion", "version", 64},
                                                      std::tuple{"BuildConfiguration", "configuration", 32}})
                    if (auto value = tagValue(xml, tag, limit); value && !trim(*value).empty())
                        engine[key] = trim(*value);
                for (const auto& [tag, key] :
                     {std::pair{"Misc.CPUBrand", "cpu"}, std::pair{"Misc.PrimaryGPUBrand", "gpu"}})
                    if (auto value = tagValue(xml, tag, 128); value && !trim(*value).empty())
                        hardware[key] = trim(*value);
                if (auto value = tagValue(xml, "MemoryStats.bIsOOM", 8)) {
                    auto flag = lower(trim(*value));
                    outOfMemory = flag == "1" || flag == "true";
                    memory["outOfMemory"] = outOfMemory;
                }
                for (const auto& [tag, key] : {std::pair{"MemoryStats.TotalPhysical", "totalPhysicalBytes"},
                                               std::pair{"MemoryStats.UsedPhysical", "usedPhysicalBytes"},
                                               std::pair{"MemoryStats.PeakUsedPhysical", "peakUsedPhysicalBytes"},
                                               std::pair{"MemoryStats.AvailablePhysical", "availablePhysicalBytes"},
                                               std::pair{"MemoryStats.OOMAllocationSize", "failedAllocationBytes"}})
                    if (auto value = number(tagValue(xml, tag, 32)); value && *value) memory[key] = *value;
                if (auto value = tagValue(xml, "ErrorMessage", 4096); value && !trim(*value).empty())
                    unreal["errorMessage"] = engineRelative(trim(*value));
                if (auto value = tagValue(xml, "CallStack", 8192); value && !trim(*value).empty())
                    unreal["callStack"] = engineRelative(trim(*value));
                if (!engine.empty()) out["engine"] = engine;
                if (!memory.empty()) out["memory"] = memory;
                if (!hardware.empty()) out["hardware"] = hardware;
                if (!unreal.empty()) out["unreal"] = unreal;
            }
        } catch (const Malformed&) {
            damaged = true;
        }
    }
    std::optional<std::uint64_t> processSeconds;
    if (auto stream = present(MiscInfoStream, 24)) {
        try {
            auto misc = in.bytes(stream->rva, 24);
            auto flags = u32(misc, 4), created = u32(misc, 12);
            if ((flags & 2) && created && timestamp >= created && timestamp - created < 30u * 86400u)
                processSeconds = timestamp - created;
            // Process identity, used only to match the crash to a recorded game start.
            if ((flags & 1) && u32(misc, 8)) out["processId"] = u32(misc, 8);
            if ((flags & 2) && created) out["processStartedAt"] = created;
        } catch (const Malformed&) {
            damaged = true;
        }
    }
    // Windows' own process start time is preferred: Unreal can still report 0
    // seconds when it fails very early.
    auto seconds = processSeconds ? processSeconds : unrealSeconds;
    if (seconds) {
        out["secondsSinceStart"] = *seconds;
        out["uptimeSource"] = processSeconds ? "process" : "unreal";
    }

    // Name third-party modules. Game, Windows, launcher and graphics-driver files
    // are always present, so they are only counted (a crash inside one is still
    // reported as the location).
    json listed = json::array();
    std::vector<std::string> injectedNames;
    for (std::size_t i = 0; i < modules.size(); ++i) {
        const auto& kind = categories[i];
        if (kind == "game" || kind == "windows" || kind == "platform" || kind == "graphics-driver" ||
            modules[i].name.empty())
            continue;
        if (listed.size() < 64)
            listed.push_back({{"name", modules[i].name}, {"category", kind}, {"label", categoryLabel(kind)}});
        if (kind == "injected") injectedNames.push_back(modules[i].name);
    }
    out["modules"] = {{"total", modules.size()}, {"listed", listed}, {"truncated", modulesTruncated}};

    std::string exceptionBytes;
    if (auto stream = present(ExceptionStream, ExceptionStreamBytes)) {
        try {
            exceptionBytes = in.bytes(stream->rva, ExceptionStreamBytes);
        } catch (const Malformed&) {
            damaged = true;
        }
    }
    out["damagedParts"] = damaged;
    json advice = json::array();
    auto addAdvice = [&](const std::string& value) {
        if (std::find(advice.begin(), advice.end(), value) == advice.end()) advice.push_back(value);
    };
    if (exceptionBytes.empty()) {
        out["status"] = "no-crash-record";
        out["headline"] = program +
                          " wrote this dump without a crash record. It may come from a freeze or a manual snapshot, "
                          "so it cannot say why the game stopped.";
        if (outOfMemory) addAdvice(OutOfMemoryAdvice);
        addAdvice("If the game froze or stopped responding, test the same spot with your mods switched off, turn off "
                  "overlays and update your graphics driver. If it only happens with mods on, Find the bad mod finds "
                  "which one.");
        out["advice"] = advice;
        return;
    }

    auto code = u32(exceptionBytes, 8);
    auto address = u64(exceptionBytes, 24);
    auto parameterCount = std::min<std::uint32_t>(u32(exceptionBytes, 32), 15);
    std::vector<std::uint64_t> parameters;
    for (std::uint32_t i = 0; i < parameterCount; ++i) parameters.push_back(u64(exceptionBytes, 40 + 8 * i));
    auto info = describe(code);
    json exception = {{"code", "0x" + hex(code, 8)}, {"name", info.name}};
    std::string explanation = info.explanation;
    if (code == 0xC0000005 && parameters.size() >= 2) {
        auto kind = parameters[0], target = parameters[1];
        const char* verb = kind == 1 ? "write to" : kind == 8 ? "run code at" : "read from";
        exception["access"] = kind == 1 ? "write" : kind == 8 ? "execute" : "read";
        exception["targetAddress"] = "0x" + hex(target);
        exception["nullAddress"] = target < 0x10000;
        explanation = std::string("It tried to ") + verb +
                      (target < 0x10000 ? " an empty (null) memory address." : " a memory address it may not use.");
    }
    if (code == 0xC0000006 && parameters.size() >= 3) exception["ioStatus"] = "0x" + hex(parameters[2], 8);
    exception["explanation"] = explanation;
    out["exception"] = exception;

    const Module* faulting = nullptr;
    std::string faultingCategory;
    for (std::size_t i = 0; i < modules.size(); ++i)
        if (modules[i].size && address >= modules[i].base && address - modules[i].base < modules[i].size) {
            faulting = &modules[i];
            faultingCategory = categories[i];
            break;
        }
    std::string where;
    if (faulting) {
        auto offset = address - faulting->base;
        auto name = faulting->name.empty() ? std::string("an unnamed module") : faulting->name;
        out["location"] = {{"module", name},
                           {"offset", "0x" + hex(offset)},
                           {"category", faultingCategory},
                           {"label", categoryLabel(faultingCategory)}};
        out["fingerprint"] = lower(hex(code, 8) + ":" + name + "+0x" + hex(offset));
        // Location only: lets two dumps of one crash (Unreal's and the crash
        // reporter's copy) be paired even if only one carries the message.
        out["locationKey"] = out["fingerprint"];
        auto lowered = lower(name);
        if ((lowered == "kernelbase.dll" || lowered == "ntdll.dll") && raisedByWindows(code)) {
            where =
                "Windows recorded the stop in " + name + ", which is where errors are raised, not where they start.";
            // Every fatal error is raised from the same place, so tell them apart
            // by the game's own message instead (digits removed, so a changing
            // size or address does not split one crash into many).
            if (out.contains("unreal") && out["unreal"].contains("errorMessage")) {
                std::string shape;
                for (char c : out["unreal"]["errorMessage"].get<std::string>().substr(0, 400))
                    if (!std::isdigit((unsigned char)c)) shape += c;
                out["fingerprint"] = lower(hex(code, 8)) + ":message-" + lower(hex(fnv1a(shape) & 0xffffffffULL, 8));
            }
        } else
            where = "It happened inside " + name + ", " + categoryLabel(faultingCategory) + ".";
    } else {
        out["location"] = {{"module", nullptr},
                           {"address", "0x" + hex(address)},
                           {"category", "outside"},
                           {"label", "outside every loaded program file"}};
        out["fingerprint"] = lower(hex(code, 8) + ":outside");
        out["locationKey"] = out["fingerprint"];
        where = modules.empty() ? "The dump does not list the loaded program files, so the location is unknown."
                                : "The crash address is outside every loaded program file, which usually means the "
                                  "game jumped to a damaged address.";
    }
    // An allocation request beyond any real PC (Unreal prints a wrapped negative
    // size as about 18 billion GB) is a data error, not a lack of RAM.
    std::optional<std::uint64_t> requested;
    if (out.contains("unreal") && out["unreal"].contains("errorMessage"))
        requested = requestedAllocation(out["unreal"]["errorMessage"].get<std::string>());
    if (!requested && out.contains("memory") && out["memory"].contains("failedAllocationBytes"))
        requested = out["memory"]["failedAllocationBytes"].get<std::uint64_t>();
    bool impossibleRequest = requested && *requested > (1ULL << 40);
    if (impossibleRequest) out["impossibleAllocation"] = true;

    std::string headline =
        seconds ? program + " crashed " + duration(*seconds) + " after starting." : program + " crashed.";
    headline += " " + explanation;
    if (code == 0x00000001 && impossibleRequest)
        headline += " It asked for an impossible amount of memory, which points to damaged or mismatched game data.";
    else if (code == 0x00000001 && outOfMemory)
        headline += " The game reported that it had run out of memory.";
    headline += " " + where;
    out["headline"] = headline;
    out["status"] = "complete";

    // Practical, cautious and deduplicated advice.
    if (impossibleRequest)
        addAdvice("The game asked for far more memory than any PC has. That is a sign of damaged or mismatched data, "
                  "such as a broken, outdated or incompatible PAK mod, rather than a real lack of RAM. Disable "
                  "recently added or updated mods and verify the game files.");
    else if (outOfMemory || code == 0xC0000017 || code == 0xC000012D)
        addAdvice(OutOfMemoryAdvice);
    if (seconds && *seconds <= 180)
        addAdvice("It crashed within " + duration(*seconds) +
                  " of starting. Early crashes usually come from something loaded at startup: if this began after "
                  "you enabled, updated or added a mod, disable that mod and start the game again.");
    if (faultingCategory == "overlay")
        addAdvice("Turn off this overlay in its program (for example Steam, Discord or a recording tool) and test "
                  "again.");
    if (faultingCategory == "graphics-driver")
        addAdvice("Update or cleanly reinstall the graphics driver and remove any GPU overclock while testing.");
    if (faultingCategory == "third-party")
        addAdvice("The crash location belongs to another program. Update or close that program and test again.");
    if (!injectedNames.empty()) {
        std::string names;
        for (const auto& name : injectedNames) names += (names.empty() ? "" : ", ") + name;
        addAdvice("Proxy DLLs in the game folder (" + names +
                  ") usually come from tools such as ReShade or script loaders. Move them out temporarily to test "
                  "whether they cause the crash.");
    }
    if (code == 0xC0000006) addAdvice("Verify the game files in Steam or Epic and check the drive for errors.");
    if (faulting && runtimeLibrary(faulting->name))
        addAdvice(faulting->name + " is a Microsoft runtime file shared by many programs. A crash inside it is "
                                   "usually caused by the code that called it, not by the file itself.");
    if (faultingCategory == "game")
        addAdvice("This location is inside the game's own code. Mods that change game data can trigger it, but the "
                  "unmodded game can crash too. If the same fingerprint appears again, note what you were doing "
                  "each time.");
    // The game's own error message often says exactly what failed.
    std::string message = out.contains("unreal") && out["unreal"].contains("errorMessage")
                              ? lower(out["unreal"]["errorMessage"].get<std::string>())
                              : "";
    auto mentions = [&](const char* word) { return message.find(word) != std::string::npos; };
    if (mentions("device_removed") || mentions("device_hung") || mentions("device lost") || mentions("gpu crash"))
        addAdvice("The graphics card stopped responding. Update or cleanly reinstall the graphics driver, remove GPU "
                  "overclocks or undervolts, lower the graphics settings and check that the card does not overheat.");
    if (mentions("outofvideomemory") || mentions("out of video memory"))
        addAdvice("The graphics card ran out of video memory. Lower texture quality or resolution in the game's "
                  "settings and test without large texture mods.");
    if (mentions(".pak") || mentions("corrupt"))
        addAdvice("The game's message points to a damaged or mismatched PAK file. Download the mod it names again, or "
                  "use Find the bad mod to find which mod it is.");
    auto forType = typeAdvice(code, exception.value("nullAddress", false));
    if (!forType.empty()) addAdvice(forType);
    if (advice.empty()) addAdvice(GeneralAdvice);
    out["advice"] = advice;
}

std::string utcText(std::uint64_t unix) {
    // Civil date from days since 1970-01-01 (Howard Hinnant's algorithm); no locale or time zone.
    auto days = (std::int64_t)(unix / 86400) + 719468, rest = (std::int64_t)(unix % 86400);
    auto era = days / 146097, doe = days - era * 146097;
    auto yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    auto doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    auto day = doy - (153 * mp + 2) / 5 + 1, month = mp < 10 ? mp + 3 : mp - 9;
    auto year = yoe + era * 400 + (month <= 2 ? 1 : 0);
    char text[40];
    std::snprintf(text, sizeof(text), "%04lld-%02lld-%02lld %02lld:%02lld:%02lld UTC", (long long)year,
                  (long long)month, (long long)day, (long long)(rest / 3600), (long long)(rest / 60 % 60),
                  (long long)(rest % 60));
    return text;
}
std::string field(const json& value, const char* key) {
    return value.is_object() && value.contains(key) && value[key].is_string() ? value[key].get<std::string>() : "";
}
std::uint64_t unsignedField(const json& value, const char* key) {
    return value.is_object() && value.contains(key) && value[key].is_number_unsigned() ? value[key].get<std::uint64_t>()
                                                                                       : 0;
}
}

json summarize(const Source& source) {
    json out = json::object();
    try {
        summarizeChecked(source, out);
    } catch (const Malformed& error) {
        json incomplete = {{"status", "incomplete"},
                           {"headline", "This crash dump is incomplete or damaged, so it cannot be summarized."},
                           {"reason", error.what()},
                           {"advice", json::array({"The game may have stopped before it finished writing the dump. "
                                                   "Other dumps from the same time may still be readable.",
                                                   GeneralAdvice})}};
        if (out.contains("crashedAt")) incomplete["crashedAt"] = out["crashedAt"];
        out = std::move(incomplete);
    }
    out["limits"] = "Function names and a readable call stack are not available: Days Gone ships without public "
                    "debug symbols. Raw memory from the dump is never read or shown.";
    return out;
}

std::string text(const json& summary) {
    std::string out;
    auto line = [&](const std::string& label, const std::string& value) {
        if (!value.empty()) out += label + ": " + value + "\n";
    };
    out += field(summary, "headline") + "\n\n";
    if (auto at = unsignedField(summary, "crashedAt")) line("Crashed at", utcText(at));
    if (summary.contains("secondsSinceStart") && summary["secondsSinceStart"].is_number_unsigned())
        line("Running time", duration(summary["secondsSinceStart"].get<std::uint64_t>()));
    if (summary.contains("exception")) {
        const auto& exception = summary["exception"];
        line("What happened",
             field(exception, "name") + " (" + field(exception, "code") + "). " + field(exception, "explanation"));
    }
    if (summary.contains("location")) {
        const auto& location = summary["location"];
        if (location.contains("module") && location["module"].is_string())
            line("Where",
                 field(location, "module") + " + " + field(location, "offset") + " (" + field(location, "label") + ")");
        else
            line("Where", "address " + field(location, "address") + " (" + field(location, "label") + ")");
    }
    line("Fingerprint", field(summary, "fingerprint"));
    if (summary.contains("memory")) {
        const auto& memory = summary["memory"];
        if (memory.contains("outOfMemory") && memory["outOfMemory"].is_boolean())
            line("Out of memory", memory["outOfMemory"].get<bool>() ? "yes" : "no");
        auto used = unsignedField(memory, "usedPhysicalBytes"), total = unsignedField(memory, "totalPhysicalBytes"),
             peak = unsignedField(memory, "peakUsedPhysicalBytes");
        if (used && total)
            line("Memory in use",
                 gigabytes(used) + " of " + gigabytes(total) + (peak ? " (peak " + gigabytes(peak) + ")" : ""));
    }
    if (summary.contains("system")) {
        const auto& system = summary["system"];
        line("System", "Windows " + field(system, "windowsVersion") + ", " +
                           std::to_string(unsignedField(system, "logicalProcessors")) + " logical processors, " +
                           field(system, "architecture"));
    }
    if (summary.contains("hardware")) {
        line("CPU", field(summary["hardware"], "cpu"));
        line("GPU", field(summary["hardware"], "gpu"));
    }
    if (summary.contains("engine")) {
        auto configuration = field(summary["engine"], "configuration");
        line("Game engine",
             field(summary["engine"], "version") + (configuration.empty() ? "" : " (" + configuration + ")"));
    }
    if (summary.contains("modules") && summary["modules"].contains("listed") &&
        summary["modules"]["listed"].is_array()) {
        std::string names;
        for (const auto& module : summary["modules"]["listed"])
            names += (names.empty() ? "" : ", ") + field(module, "name") + " (" + field(module, "label") + ")";
        line("Other loaded programs", names.empty() ? "none" : names);
    }
    line("Reason", field(summary, "reason"));
    if (summary.contains("unreal")) {
        line("Game error message", field(summary["unreal"], "errorMessage"));
        if (!field(summary["unreal"], "callStack").empty())
            out += "Game call stack:\n" + field(summary["unreal"], "callStack") + "\n";
    }
    if (summary.contains("sameCrash") && summary["sameCrash"].is_object()) {
        auto count = unsignedField(summary["sameCrash"], "occurrences");
        if (count > 1) out += "This crash location appears " + std::to_string(count) + " times in the listed dumps.\n";
    }
    if (summary.contains("advice") && summary["advice"].is_array() && !summary["advice"].empty()) {
        out += "\nWhat you can try:\n";
        for (const auto& item : summary["advice"])
            if (item.is_string()) out += "- " + item.get<std::string>() + "\n";
    }
    out += "\n" + field(summary, "limits") + "\n";
    return out;
}
}
