#include "nxm.hpp"
#include "platform_posix.hpp"
#include <QProcess>
#include <QStringList>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace drifter {
namespace {
namespace fs = std::filesystem;
using json = nlohmann::json;
const std::string desktopId = "drifters-toolkit.desktop", mime = "x-scheme-handler/nxm";
fs::path home() {
    const char* value = getenv("HOME");
    if (!value || !fs::path(value).is_absolute()) throw std::runtime_error("Linux could not locate your home folder.");
    return value;
}
fs::path base(const char* variable, const fs::path& fallback) {
    const char* value = getenv(variable);
    return value && fs::path(value).is_absolute() ? fs::path(value) : fallback;
}
fs::path desktopPath() {
    return base("XDG_DATA_HOME", home() / ".local/share") / "applications" / desktopId;
}
fs::path associations() {
    return base("XDG_CONFIG_HOME", home() / ".config") / "mimeapps.list";
}
bool below(const fs::path& path, const fs::path& parent) {
    auto relative = path.lexically_relative(parent);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}
bool stableExecutable(const fs::path& executable) {
    if (const char* image = getenv("APPIMAGE"); image && *image) return false;
    for (const auto& component : executable) {
        auto value = component.string();
        if (value.rfind(".mount_", 0) == 0 || value.rfind("appimage_extracted_", 0) == 0) return false;
    }
    for (const auto& root : {fs::path("/tmp"), fs::path("/var/tmp"), fs::path("/dev/shm"), fs::path("/run")})
        if (below(executable, root)) return false;
    std::error_code issue;
    auto temporary = fs::temp_directory_path(issue);
    if (issue || !temporary.is_absolute()) return false;
    auto canonical = fs::weakly_canonical(temporary, issue);
    return !issue && !below(executable, canonical);
}
std::string query() {
    QProcess process;
    process.start("xdg-mime", {"query", "default", QString::fromStdString(mime)});
    if (!process.waitForFinished(5000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        throw std::runtime_error(
            "Your desktop could not report its Nexus download handler. No association was changed.");
    auto value = process.readAllStandardOutput().trimmed();
    if (value.size() > 512) throw std::runtime_error("The desktop handler response was invalid.");
    return value.toStdString();
}
std::string entry(const fs::path& executable) {
    auto value = executable.string();
    if (value.find_first_of("\n\r\t") != std::string::npos)
        throw std::runtime_error("The executable path cannot be registered safely.");
    std::string escaped;
    for (char c : value) {
        if (c == '%')
            escaped += "%%";
        else {
            if (c == '\\' || c == '\"' || c == '`' || c == '$') escaped += '\\';
            escaped += c;
        }
    }
    return "[Desktop Entry]\nType=Application\nName=Drifter’s Toolkit\nComment=Days Gone mod loader\nExec=\"" +
           escaped + "\" --nxm %u\nIcon=" + executable.parent_path().string() +
           "/ui/assets/icon.png\nTerminal=false\nNoDisplay=true\nMimeType=x-scheme-handler/"
           "nxm;\nCategories=Game;Utility;\n";
}
void ownFile(const fs::path& path) {
    posix::requirePlainPath(path);
    struct stat info{};
    if (lstat(path.c_str(), &info) == 0 && (info.st_uid != getuid() || !S_ISREG(info.st_mode) || info.st_nlink != 1))
        throw std::runtime_error("An association file is not an ordinary file owned by this user. It was preserved.");
}
void setAssociation(bool add) {
    auto path = associations();
    ownFile(path);
    std::string original = fs::exists(path) ? posix::readText(path, 1024 * 1024) : "";
    std::string output;
    bool section = false, found = false, inserted = false;
    size_t cursor = 0;
    while (cursor < original.size()) {
        auto end = original.find('\n', cursor);
        if (end == std::string::npos) end = original.size();
        auto line = original.substr(cursor, end - cursor);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line.front() == '[') {
            if (section && add && !inserted) {
                output += mime + "=" + desktopId + ";\n";
                inserted = true;
            }
            section = line == "[Default Applications]";
            found = found || section;
        }
        if (section && line.rfind(mime + "=", 0) == 0) {
            auto value = line.substr(mime.size() + 1);
            if (value != desktopId && value != desktopId + ";")
                throw std::runtime_error(
                    "Another mod manager has changed this handler. Its association was preserved.");
            if (add) {
                output += line + "\n";
                inserted = true;
            }
        } else
            output += line + "\n";
        cursor = end < original.size() ? end + 1 : end;
    }
    if (add && !inserted) {
        if (!found) output += "[Default Applications]\n";
        output += mime + "=" + desktopId + ";\n";
    }
    fs::create_directories(path.parent_path());
    posix::requirePlainPath(path.parent_path());
    posix::atomicWrite(path, output);
}
}
NxmIntegration::NxmIntegration(fs::path executable) : executable_(fs::canonical(executable)) {}
json NxmIntegration::status() {
    if (!stableExecutable(executable_))
        return {{"registered", false},
                {"available", false},
                {"reason", "Browser download registration is unavailable for this AppImage or temporary location. "
                           "Paste an authorised Nexus link in Downloads, or download on the Nexus website and "
                           "import the file."}};
    auto handler = query();
    bool file = fs::exists(desktopPath());
    bool owned = handler == desktopId && file;
    if (owned && file) {
        ownFile(desktopPath());
        owned = posix::readText(desktopPath(), 16384) == entry(executable_);
    }
    return {{"registered", owned},
            {"available", (handler.empty() && !file) || owned},
            {"reason", owned                      ? "This toolkit handles Nexus Mod Manager Download links."
                       : handler.empty() && !file ? "No Nexus download handler is registered. You can opt in below."
                                                  : "Another application handles Nexus download links. It will be "
                                                    "preserved; use an authorised link here instead."}};
}
json NxmIntegration::registerHandler() {
    auto current = status();
    if (current.value("registered", false)) return current;
    if (!current.value("available", false))
        throw std::runtime_error(
            current.value("reason", "Browser download integration is unavailable for this installation."));
    auto path = desktopPath();
    posix::requirePlainPath(path);
    if (fs::exists(path))
        throw std::runtime_error("A toolkit desktop entry already exists. It was preserved rather than overwritten.");
    fs::create_directories(path.parent_path());
    posix::requirePlainPath(path.parent_path());
    posix::atomicWrite(path, entry(executable_));
    try {
        auto currentHandler = query();
        if (!currentHandler.empty() && currentHandler != desktopId)
            throw std::runtime_error("Another application registered a download handler while this dialog was open. "
                                     "Its association was preserved.");
        ownFile(path);
        if (posix::readText(path, 16384) != entry(executable_))
            throw std::runtime_error("The new desktop entry changed before registration. It was preserved.");
        setAssociation(true);
    } catch (...) {
        try {
            ownFile(path);
            if (fs::exists(path) && posix::readText(path, 16384) == entry(executable_)) unlink(path.c_str());
        } catch (...) {}
        throw;
    }
    return status();
}
json NxmIntegration::unregisterHandler() {
    auto current = status();
    if (!current.value("registered", false))
        throw std::runtime_error("This executable does not own the current Nexus handler. No association was changed.");
    auto path = desktopPath();
    ownFile(path);
    if (posix::readText(path, 16384) != entry(executable_))
        throw std::runtime_error("The desktop entry changed and was preserved.");
    setAssociation(false);
    if (unlink(path.c_str()))
        throw std::runtime_error("The association was cleared, but its desktop entry could not be removed.");
    return status();
}
json NxmIntegration::handle(const json& request) {
    try {
        auto action = request.value("action", "");
        if (action == "nxmStatus") return {{"ok", true}, {"data", status()}};
        if (!request.value("confirmed", false))
            throw std::runtime_error("Review and confirm the desktop association first.");
        if (action == "nxmRegister") return {{"ok", true}, {"data", registerHandler()}};
        if (action == "nxmUnregister") return {{"ok", true}, {"data", unregisterHandler()}};
        throw std::runtime_error("Unknown Nexus association request.");
    } catch (const std::exception& issue) {
        return {{"ok", false}, {"error", issue.what()}};
    }
}
}
