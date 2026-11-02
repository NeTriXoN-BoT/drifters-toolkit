#pragma once
// Shared synthetic minidump builder for tests. Produces small MDMP files with
// known private markers that a summary must never expose, and a private
// memory region whose position is recorded so tests can prove it is never read.
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace minidump_fixture {
inline void put16(std::string& b, std::uint16_t v) {
    b += (char)(v & 255);
    b += (char)(v >> 8);
}
inline void put32(std::string& b, std::uint32_t v) {
    put16(b, (std::uint16_t)(v & 0xffff));
    put16(b, (std::uint16_t)(v >> 16));
}
inline void put64(std::string& b, std::uint64_t v) {
    put32(b, (std::uint32_t)(v & 0xffffffffULL));
    put32(b, (std::uint32_t)(v >> 32));
}
inline void set32(std::string& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = (char)((v >> (8 * i)) & 255);
}
inline std::uint32_t get32(const std::string& b, std::size_t at) {
    return (std::uint32_t)(unsigned char)b[at] | ((std::uint32_t)(unsigned char)b[at + 1] << 8) |
           ((std::uint32_t)(unsigned char)b[at + 2] << 16) | ((std::uint32_t)(unsigned char)b[at + 3] << 24);
}
// Offset of the directory entry for a stream type (header field 8 = count).
inline std::size_t entryOf(const std::string& file, std::uint32_t type) {
    for (std::uint32_t i = 0; i < get32(file, 8); ++i)
        if (get32(file, 32 + i * 12) == type) return 32 + i * 12;
    throw std::runtime_error("fixture stream not found");
}
inline std::string utf16(const std::u16string& value) {
    std::string out;
    for (char16_t c : value) put16(out, (std::uint16_t)c);
    return out;
}

struct ModuleSpec {
    std::uint64_t base, size;
    std::u16string path;
};
struct Spec {
    std::uint32_t timestamp = 1792502768; // 2026-10-20 13:26:08 UTC
    std::vector<ModuleSpec> modules;
    bool exception = true;
    std::uint32_t code = 0xC0000005;
    std::uint64_t address = 0;
    std::vector<std::uint64_t> parameters = {0, 0};
    bool misc = true;
    std::uint32_t processSeconds = 56;
    std::string crashContext; // UTF-8 unless utf16Context
    bool utf16Context = false;
    bool memoryList = true; // a private memory region that must never be read
};
inline const std::uint64_t GameBase = 0x140000000ULL;
inline const std::string PrivateMemory = "PRIVATE-PROCESS-MEMORY-MUST-NEVER-BE-READ";
inline std::uint64_t memoryStart = 0, memoryEnd = 0; // set by build() for the read guard

inline std::vector<ModuleSpec> defaultModules() {
    return {{GameBase, 0x5000000, u"D:\\Games\\Days Gone\\BendGame\\Binaries\\Win64\\DaysGone.exe"},
            {0x7ff600000000ULL, 0x200000, u"D:\\Games\\Days Gone\\Engine\\Binaries\\ThirdParty\\PhysX\\PhysX3_x64.dll"},
            {0x7ffa00000000ULL, 0x400000, u"C:\\Windows\\System32\\KERNELBASE.dll"},
            {0x7ffa10000000ULL, 0x100000, u"C:\\Program Files (x86)\\Steam\\gameoverlayrenderer64.dll"},
            {0x7ffa20000000ULL, 0x100000, u"D:\\Games\\Days Gone\\BendGame\\Binaries\\Win64\\dxgi.dll"},
            {0x7ffa30000000ULL, 0x900000, u"C:\\Windows\\System32\\DriverStore\\FileRepository\\nv\\nvwgf2umx.dll"},
            {0x7ffa40000000ULL, 0x100000, u"C:\\Tools\\Private Person\\M\u00f6dHook.dll"},
            {0x7ffa50000000ULL, 0x100000, u"C:\\ProgramData\\Microsoft\\Windows Defender\\Platform\\MpOAV.dll"},
            {0x7ffa60000000ULL, 0x100000, u"C:\\Program Files (x86)\\Steam\\steamclient64.dll"}};
}
inline std::string defaultContext(const std::string& errorMessage = "", const char* outOfMemory = "0") {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<FGenericCrashContext>\r\n<RuntimeProperties>\r\n"
           "<SecondsSinceStart>99</SecondsSinceStart>\r\n<EngineVersion>4.11.1-0+UE4</EngineVersion>\r\n"
           "<BuildConfiguration>Shipping</BuildConfiguration>\r\n"
           "<CommandLine>-Steamworks -key=private-command-line</CommandLine>\r\n"
           "<UserName>private-user-name</UserName>\r\n<BaseDir>C:/Users/private-user-name/Game/</BaseDir>\r\n"
           "<MachineId>PRIVATE-MACHINE-ID</MachineId>\r\n<EpicAccountId>PRIVATE-EPIC-ACCOUNT</EpicAccountId>\r\n"
           "<ErrorMessage>" +
           errorMessage +
           "</ErrorMessage>\r\n<CallStack></CallStack>\r\n"
           "<Misc.CPUBrand>Fixture CPU 9000</Misc.CPUBrand>\r\n<Misc.PrimaryGPUBrand>Fixture "
           "GPU</Misc.PrimaryGPUBrand>\r\n"
           "<MemoryStats.TotalPhysical>51539607552</MemoryStats.TotalPhysical>\r\n"
           "<MemoryStats.UsedPhysical>4724464025</MemoryStats.UsedPhysical>\r\n"
           "<MemoryStats.PeakUsedPhysical>5368709120</MemoryStats.PeakUsedPhysical>\r\n"
           "<MemoryStats.bIsOOM>" +
           outOfMemory + "</MemoryStats.bIsOOM>\r\n" + "</RuntimeProperties>\r\n</FGenericCrashContext>\r\n";
}
inline Spec defaultSpec() {
    Spec spec;
    spec.modules = defaultModules();
    spec.address = GameBase + 0x904178;
    spec.crashContext = defaultContext();
    return spec;
}

// Layout: header, directory, module names, then each stream (8-byte aligned).
inline std::string build(const Spec& spec) {
    struct Stream {
        std::uint32_t type;
        std::string data;
    };
    std::vector<Stream> streams;
    std::string system;
    put16(system, 9);
    put16(system, 25);
    put16(system, 0);
    system += (char)24;
    system += (char)1;
    put32(system, 10);
    put32(system, 0);
    put32(system, 26300);
    put32(system, 2);
    put32(system, 0);
    system.resize(56, '\0');
    streams.push_back({7, system});
    if (spec.misc) {
        std::string misc;
        put32(misc, 24);
        put32(misc, 2); // MINIDUMP_MISC1_PROCESS_TIMES
        put32(misc, 4242);
        put32(misc, spec.timestamp - spec.processSeconds);
        put32(misc, 1);
        put32(misc, 1);
        streams.push_back({15, misc});
    }
    if (spec.exception) {
        std::string exception;
        put32(exception, 20540);
        put32(exception, 0);
        put32(exception, spec.code);
        put32(exception, 0);
        put64(exception, 0);
        put64(exception, spec.address);
        put32(exception, (std::uint32_t)spec.parameters.size());
        put32(exception, 0);
        for (std::size_t i = 0; i < 15; ++i) put64(exception, i < spec.parameters.size() ? spec.parameters[i] : 0);
        put32(exception, 0);
        put32(exception, 0);
        streams.push_back({6, exception});
    }
    if (!spec.crashContext.empty()) {
        std::string context;
        if (spec.utf16Context) {
            context = "\xff\xfe";
            for (unsigned char c : spec.crashContext) put16(context, c);
        } else
            context = spec.crashContext;
        streams.push_back({0x10000, context});
    }
    if (spec.memoryList) streams.push_back({5, PrivateMemory});
    std::size_t moduleStream = spec.modules.empty() ? SIZE_MAX : streams.size();
    if (!spec.modules.empty()) streams.push_back({4, std::string(4 + spec.modules.size() * 108, '\0')});

    std::string file(32 + streams.size() * 12, '\0');
    std::vector<std::uint32_t> nameRvas;
    for (const auto& module : spec.modules) {
        nameRvas.push_back((std::uint32_t)file.size());
        auto name = utf16(module.path);
        put32(file, (std::uint32_t)name.size());
        file += name;
        file += std::string(2, '\0');
    }
    if (moduleStream != SIZE_MAX) {
        auto& table = streams[moduleStream].data;
        set32(table, 0, (std::uint32_t)spec.modules.size());
        for (std::size_t i = 0; i < spec.modules.size(); ++i) {
            std::string record;
            put64(record, spec.modules[i].base);
            put32(record, (std::uint32_t)spec.modules[i].size);
            put32(record, 0);
            put32(record, 0);
            put32(record, nameRvas[i]);
            record.resize(108, '\0');
            table.replace(4 + i * 108, 108, record);
        }
    }
    std::string header;
    put32(header, 0x504D444D);
    put32(header, 0xA793);
    put32(header, (std::uint32_t)streams.size());
    put32(header, 32);
    put32(header, 0);
    put32(header, spec.timestamp);
    put64(header, 0);
    file.replace(0, 32, header);
    for (std::size_t i = 0; i < streams.size(); ++i) {
        while (file.size() % 8) file += '\0';
        auto rva = (std::uint32_t)file.size();
        if (streams[i].type == 5) {
            memoryStart = rva;
            memoryEnd = rva + streams[i].data.size();
        }
        std::string entry;
        put32(entry, streams[i].type);
        put32(entry, (std::uint32_t)streams[i].data.size());
        put32(entry, rva);
        file.replace(32 + i * 12, 12, entry);
        file += streams[i].data;
    }
    return file;
}

}
