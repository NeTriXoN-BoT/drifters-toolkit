// Included inside Manager::Impl. All filesystem paths and approval metadata in
// these plans originate in native state, never in a renderer/archive assertion.
static bool backupProfileId(const std::string& id) {
    return id.size() == 32 &&
           std::all_of(id.begin(), id.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
static std::string backupLabel(const json& value, std::size_t limit, const std::string& fallback) {
    if (!value.is_string()) fail("The library backup has an invalid name.");
    const auto text = value.get<std::string>();
    if (text.size() > limit ||
        std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        fail("A name in the library backup is too long or contains control characters.");
    return text.empty() ? fallback : text;
}
LibraryBackup::Source libraryBackupSource() {
    LibraryBackup::Source result;
    result.portable = {{"mods", json::array()},
                       {"profiles", json::array()},
                       {"selectionProfiles", json::object()},
                       {"notes", library_meta::empty()},
                       {"preferences", json::object()}};
    std::map<std::string, std::string> identities, modKeys;
    std::uint64_t uniquePackageBytes = 0;
    auto add = [&](const fs::path& path, const json& mod, const std::string& filename,
                   const std::string& expected) -> std::string {
        if (!plainName(filename) || protectedBasePackage(filename) ||
            lower(fs::u8path(filename).extension().u8string()) != ".pak" || !hashValid(expected))
            fail("A library package has an invalid backup record.");
        const auto originalId = mod.at("id").get<std::string>();
        if (!plainName(originalId)) fail("A backup package has an invalid original ID.");
        const auto identity = expected + "\n" + filename;
        auto found = identities.find(identity);
        auto original = modKeys.find(originalId);
        if (original != modKeys.end() && (found == identities.end() || original->second != found->second))
            fail("The profiles reuse a package ID for different files. Review those profiles before backing up.");
        if (found == identities.end() && result.packages.size() >= 4096)
            fail("The library contains more than 4,096 package versions. No backup was created.");
        safeRegular(path);
        const auto bytes = fs::file_size(path);
        if (bytes > 512ull * 1024 * 1024) fail("A library package exceeds the 512 MB backup limit.");
        if (found == identities.end()) {
            if (bytes > 16ull * 1024 * 1024 * 1024 - uniquePackageBytes)
                fail("The library exceeds the 16 GB backup limit.");
            uniquePackageBytes += bytes;
        }
        operationProgress("checking", filename, result.packages.size(), 0,
                          "Checking library packages and saved profiles…");
        if (sha256File(path) != expected)
            fail("A library package changed. Resolve its integrity warning before backing up the library.");
        auto assets = inspectPak(path).assets;
        if (assets.empty() || json(assets) != mod.at("assets"))
            fail("A library package index changed. Its original file was preserved.");
        if (found != identities.end()) {
            modKeys[mod.at("id").get<std::string>()] = found->second;
            return found->second;
        }
        const auto key = "package-" + std::to_string(result.packages.size() + 1);
        identities[identity] = key;
        modKeys[mod.at("id").get<std::string>()] = key;
        result.packages.push_back({key, filename, path, expected});
        result.portable["mods"].push_back({{"key", key},
                                           {"originalId", mod.at("id")},
                                           {"name", backupLabel(mod.at("name"), 180, filename)},
                                           {"version", backupLabel(mod.value("version", json("")), 100, "")}});
        return key;
    };
    for (const auto& mod : catalog) {
        const auto path = cached(mod);
        if (fs::exists(path))
            add(path, mod, mod.at("packageFile"), mod.at("sha256"));
        else if (state["owned"].contains(mod.at("id").get<std::string>()))
            add(installed(mod), mod, mod.at("packageFile"), mod.at("sha256"));
    }
    json owners =
        fs::exists(stateDir / "profiles-owner.json") ? readJson(stateDir / "profiles-owner.json") : json::object();
    if (!owners.is_object() || owners.size() > 256) fail("The portable profile ownership records are invalid.");
    std::set<std::string> profileIds;
    for (auto item = owners.begin(); item != owners.end(); ++item) {
        const auto id = item.key();
        if (!backupProfileId(id) || !item.value().is_string() || !hashValid(item.value().get<std::string>()))
            fail("A portable profile ownership record is invalid.");
        profileIds.insert(id);
    }
    const auto profileRoot = stateDir / "profiles";
    if (fs::exists(profileRoot)) {
        safePath(profileRoot);
        if (!fs::is_directory(profileRoot))
            fail("The portable profiles folder is not a regular folder. No backup was created.");
        std::size_t visited = 0;
        for (const auto& entry : fs::directory_iterator(profileRoot)) {
            if (++visited > 512) fail("There are too many items in the portable profiles folder to back it up safely.");
            safePath(entry.path());
            const auto name = entry.path().filename().u8string();
            // Profiles::remove moves its already-reviewed folder outside the
            // discovery namespace before committing removal. These private
            // cleanup tombs are not live profiles and never gain new ownership.
            if (name.size() == 41 && name.substr(32) == ".removing" && backupProfileId(name.substr(0, 32)) &&
                fs::is_directory(entry.path()))
                continue;
            if (!backupProfileId(name) || !fs::is_directory(entry.path()))
                fail("The portable profiles folder contains an unrecognized item. Review that folder before creating a "
                     "complete library backup.");
            profileIds.insert(name);
        }
    }
    if (profileIds.size() > 256)
        fail(
            "The library has more than 256 portable profiles. Reduce that number before creating one complete backup.");
    for (const auto& id : profileIds) {
        const auto directory = stateDir / "profiles" / fs::u8path(id);
        const auto file = directory / "manifest.json";
        safeRegular(file);
        if (fs::file_size(file) > 8u * 1024u * 1024u)
            fail("A portable profile manifest exceeds the 8 MB limit. No backup was created.");
        if (owners.contains(id) && sha256File(file) != owners[id])
            fail("A portable profile changed. Review that profile before backing it up.");
        auto value = readJson(file);
        if (value.value("schemaVersion", 0) != 1 || value.value("id", "") != id || !value.at("mods").is_array() ||
            value["mods"].size() > 256)
            fail("A portable profile manifest is invalid.");
        const auto name = backupLabel(value.at("name"), 64, "Restored profile");
        if (!plainName(name)) fail("A portable profile name is invalid.");
        json packages = json::array();
        std::set<std::string> names, ids;
        std::set<std::wstring> expectedFiles = {nativePathKey(file)};
        for (const auto& captured : value["mods"]) {
            if (!captured.is_object() || !captured.at("id").is_string() ||
                !plainName(captured["id"].get<std::string>()) || !ids.insert(captured["id"].get<std::string>()).second)
                fail("A portable profile has invalid or repeated package IDs. No backup was created.");
            const auto filename = captured.at("filename").get<std::string>(),
                       hash = captured.at("sha256").get<std::string>();
            if (!plainName(filename) || protectedBasePackage(filename) ||
                lower(fs::u8path(filename).extension().u8string()) != ".pak")
                fail("A portable profile contains an unsafe package filename.");
            if (!names.insert(lower(filename)).second) fail("A profile has duplicate package filenames.");
            const auto path = directory / "paks" / fs::u8path(filename);
            expectedFiles.insert(nativePathKey(path));
            safeRegular(path);
            if (!captured.at("bytes").is_number_unsigned() ||
                fs::file_size(path) != captured["bytes"].get<std::uint64_t>())
                fail("A profile package changed before backup.");
            packages.push_back({{"key", add(path, captured, filename, hash)}});
        }
        std::size_t visited = 0;
        for (const auto& entry : fs::recursive_directory_iterator(directory)) {
            if (++visited > 1024) fail("A portable profile contains too many files to back up safely.");
            safePath(entry.path());
            if (fs::is_directory(entry.path())) {
                if (nativePathKey(entry.path()) != nativePathKey(directory / "paks"))
                    fail("A portable profile contains an unexpected folder. Review it before backing up.");
            } else if (!fs::is_regular_file(entry.path()) || !expectedFiles.count(nativePathKey(entry.path())))
                fail("A portable profile contains unexpected files. Review it before backing up; no files were omitted "
                     "or deleted.");
        }
        result.portable["profiles"].push_back({{"name", name}, {"mods", packages}});
    }
    for (auto profile = state["profiles"].begin(); profile != state["profiles"].end(); ++profile) {
        json keys = json::array();
        for (const auto& id : profile.value()) {
            auto found = modKeys.find(id.get<std::string>());
            if (found == modKeys.end())
                fail("A selection profile refers to a package missing from the library. Import that version or update "
                     "the profile before backing up.");
            keys.push_back(found->second);
        }
        result.portable["selectionProfiles"][profile.key()] = keys;
    }
    const auto meta = fs::exists(stateDir / "library-preferences.json")
                          ? library_meta::validate(readJson(stateDir / "library-preferences.json"))
                          : library_meta::empty();
    result.portable["notes"]["entries"] = meta["entries"];
    const auto settings = effectiveSettings();
    for (const auto* key : {"seasonalThemes", "seasonalEffects", "theme", "colorMode", "useSystemTheme",
                            "reducedMotion", "compactMode", "fontScale", "interfaceScale", "autoScale", "highContrast"})
        if (settings.contains(key)) result.portable["preferences"][key] = settings[key];
    (void)preferencePatch(result.portable["preferences"], preferenceDefaults());
    return result;
}
std::vector<LibraryBackup::Write> libraryBackupPlan(const LibraryBackup::VerifiedBackup& backup) {
    const auto& portable = backup.portable;
    const std::set<std::string> top = {"mods", "profiles", "selectionProfiles", "notes", "preferences"};
    if (!portable.is_object() || portable.size() != 5) fail("This backup is missing portable library metadata.");
    for (auto it = portable.begin(); it != portable.end(); ++it)
        if (!top.count(it.key())) fail("This backup contains unsupported metadata.");
    if (!portable.at("mods").is_array() || portable["mods"].size() != backup.packages.size() ||
        !portable.at("profiles").is_array() || portable["profiles"].size() > 256 ||
        !portable.at("selectionProfiles").is_object() || portable["selectionProfiles"].size() > 256)
        fail("The portable profiles in the backup are invalid.");
    const std::set<std::string> appearance = {"seasonalThemes", "seasonalEffects", "theme",       "colorMode",
                                              "useSystemTheme", "reducedMotion",   "compactMode", "fontScale",
                                              "interfaceScale", "autoScale",       "highContrast"};
    if (!portable.at("preferences").is_object()) fail("The portable appearance settings are invalid.");
    for (auto it = portable["preferences"].begin(); it != portable["preferences"].end(); ++it)
        if (!appearance.count(it.key())) fail("Only appearance settings can be carried in a library backup.");
    (void)preferencePatch(portable["preferences"], preferenceDefaults());
    auto next = state, nextLocal = localCatalog;
    std::vector<LibraryBackup::Write> writes;
    std::map<std::string, json> mapped;
    std::map<std::string, const LibraryBackup::VerifiedPackage*> packages;
    for (const auto& p : backup.packages) packages.emplace(p.key, &p);
    std::map<std::string, json> labels;
    std::map<std::string, std::string> noteKeys;
    for (const auto& record : portable["mods"]) {
        if (!record.is_object() || record.size() != 4 || !record.at("key").is_string() ||
            !record.at("originalId").is_string() || !library_meta::validId(record["originalId"].get<std::string>()) ||
            !record.at("name").is_string() || !record.at("version").is_string())
            fail("A backup mod label is invalid.");
        auto key = record["key"].get<std::string>();
        if (!packages.count(key) || !labels.emplace(key, record).second)
            fail("The backup repeats or omits a mod label.");
        if (!noteKeys.emplace(record["originalId"].get<std::string>(), key).second)
            fail("The backup repeats a personal-note identity.");
        backupLabel(record["name"], 180, "Local mod");
        backupLabel(record["version"], 100, "");
    }
    std::set<std::string> planned;
    for (const auto& p : backup.packages) {
        if (!plainName(p.filename) || protectedBasePackage(p.filename) || p.assets.empty())
            fail("A backup package cannot be managed as a mod.");
        refuseBlockedLocalBytes(p.sha256);
        json mod;
        for (const auto& known : catalog)
            if (known.value("sha256", "") == p.sha256 && known.value("packageFile", "") == p.filename &&
                known.value("available", true) && json(p.assets) == known.at("assets")) {
                mod = known;
                break;
            }
        if (mod.is_null()) {
            const auto id = localId(p.sha256, p.filename);
            for (const auto& known : nextLocal)
                if (known["id"] == id) {
                    if (known.at("packageFile") != p.filename || known.at("sha256") != p.sha256 ||
                        known.at("assets") != json(p.assets))
                        fail("A backup package reuses a local identity with different package metadata. Existing files "
                             "were preserved.");
                    mod = known;
                    break;
                }
            if (mod.is_null()) {
                mod = {{"id", id},
                       {"name", backupLabel(labels.at(p.key).at("name"), 180,
                                            readableName(fs::u8path(p.filename).stem().u8string()))},
                       {"version", ""},
                       {"category", "Local mods"},
                       {"summary",
                        "Restored from a private library backup. Check the original mod page for updates and support."},
                       {"description", "A locally restored PAK. Its asset index and file hash were checked here; the "
                                       "backup cannot establish its author, origin or version."},
                       {"available", true},
                       {"localManaged", true},
                       {"identityStatus", "local-import"},
                       {"packageFile", p.filename},
                       {"sha256", p.sha256},
                       {"packageSize", p.bytes},
                       {"assets", p.assets},
                       {"provenance",
                        {{"kind", "local-import"}, {"source", "user-selected-file"}, {"sourceSha256", p.sha256}}}};
                for (const auto& known : catalog)
                    if (lower(known.value("packageFile", "")) == lower(p.filename)) {
                        auto group = known.value("variantGroup", "");
                        mod["variantGroup"] = group.empty() ? "filename:" + lower(p.filename) : group;
                        break;
                    }
                validateLocalMod(mod);
                nextLocal.push_back(mod);
            }
        }
        const auto id = mod.at("id").get<std::string>();
        mapped[p.key] = mod;
        const auto relative = "library/" + id + "/" + p.filename;
        const auto target = stateDir / fs::u8path(relative);
        if (fs::exists(target)) {
            safeRegular(target);
            if (sha256File(target) != p.sha256 || json(inspectPak(target).assets) != json(p.assets))
                fail("An existing library package changed and will not be overwritten by a backup.");
        } else if (planned.insert(relative).second)
            writes.push_back({relative, p.path, {}});
        next["cached"][id] = ownership(mod);
    }
    auto notes = library_meta::validate(portable.at("notes"));
    auto currentNotes = fs::exists(stateDir / "library-preferences.json")
                            ? library_meta::validate(readJson(stateDir / "library-preferences.json"))
                            : library_meta::empty();
    for (auto note = notes["entries"].begin(); note != notes["entries"].end(); ++note) {
        auto key = noteKeys.find(note.key());
        const auto id = key == noteKeys.end() ? note.key() : mapped.at(key->second).at("id").get<std::string>();
        if (!currentNotes["entries"].contains(id)) currentNotes["entries"][id] = note.value();
    }
    library_meta::validate(currentNotes);
    writes.push_back({"library-preferences.json", {}, currentNotes.dump(2)});
    auto owners =
        fs::exists(stateDir / "profiles-owner.json") ? readJson(stateDir / "profiles-owner.json") : json::object();
    if (!owners.is_object() || owners.size() > 256) fail("The current portable profile ownership records are invalid.");
    std::set<std::string> names;
    for (auto old = owners.begin(); old != owners.end(); ++old) {
        if (!backupProfileId(old.key()) || !old.value().is_string() || !hashValid(old.value().get<std::string>()))
            fail("A current profile ownership record is invalid.");
        auto manifest = stateDir / "profiles" / fs::u8path(old.key()) / "manifest.json";
        safeRegular(manifest);
        if (sha256File(manifest) != old.value()) fail("An existing portable profile changed and was preserved.");
        auto value = readJson(manifest);
        names.insert(lower(value.at("name").get<std::string>()));
    }
    auto uniqueName = [&](const std::string& requested, std::set<std::string>& used) {
        std::string name = requested;
        if (!plainName(name) || name.size() > 64) fail("A restored profile has an invalid name.");
        for (unsigned n = 2; used.count(lower(name)); ++n) {
            auto suffix = " (restored " + std::to_string(n) + ")";
            name = requested.substr(0, std::min(requested.size(), 64 - suffix.size())) + suffix;
        }
        used.insert(lower(name));
        return name;
    };
    if (portable["profiles"].size() > 256 - owners.size())
        fail("Restoring these profiles would exceed the 256 profile limit.");
    for (const auto& profile : portable["profiles"]) {
        if (!profile.is_object() || profile.size() != 2 || !profile.at("name").is_string() ||
            !profile.at("mods").is_array() || profile["mods"].size() > 256)
            fail("A portable profile in the backup is invalid.");
        auto name = uniqueName(backupLabel(profile["name"], 64, "Restored profile"), names);
        std::string id;
        do {
            id = randomHex().substr(0, 32);
        } while (fs::exists(stateDir / "profiles" / fs::u8path(id)) || owners.contains(id));
        json entries = json::array();
        std::set<std::string> files, ids;
        for (const auto& reference : profile["mods"]) {
            if (!reference.is_object() || reference.size() != 1 || !reference.at("key").is_string() ||
                !mapped.count(reference["key"].get<std::string>()))
                fail("A profile references a missing backup package.");
            const auto key = reference["key"].get<std::string>();
            const auto& mod = mapped.at(key);
            const auto& p = *packages.at(key);
            auto modId = mod.at("id").get<std::string>();
            if (!files.insert(lower(p.filename)).second || !ids.insert(modId).second)
                fail("A restored profile contains duplicate packages.");
            json entry = {{"id", modId},
                          {"name", mod.at("name")},
                          {"version", mod.value("version", "")},
                          {"filename", p.filename},
                          {"sha256", p.sha256},
                          {"bytes", p.bytes},
                          {"assets", p.assets},
                          {"identityStatus", mod.value("identityStatus", "local-release-audited")}};
            if (mod.contains("provenance")) entry["provenance"] = mod["provenance"];
            entries.push_back(entry);
            writes.push_back({"profiles/" + id + "/paks/" + p.filename, p.path, {}});
        }
        auto text = json{{"schemaVersion", 1}, {"id", id}, {"name", name}, {"mods", entries}}.dump(2);
        owners[id] = hexBytes(digestBytes(text));
        writes.push_back({"profiles/" + id + "/manifest.json", {}, text});
    }
    writes.push_back({"profiles-owner.json", {}, owners.dump(2)});
    std::set<std::string> selectionNames;
    for (auto it = next["profiles"].begin(); it != next["profiles"].end(); ++it) selectionNames.insert(lower(it.key()));
    for (auto selection = portable["selectionProfiles"].begin(); selection != portable["selectionProfiles"].end();
         ++selection) {
        if (!selection.value().is_array() || selection.value().size() > 256)
            fail("A backup selection profile is invalid.");
        const auto name = uniqueName(selection.key(), selectionNames);
        std::set<std::string> chosen;
        for (const auto& key : selection.value()) {
            if (!key.is_string() || !mapped.count(key.get<std::string>()))
                fail("A selection profile references a missing backup package.");
            if (!chosen.insert(mapped.at(key.get<std::string>()).at("id").get<std::string>()).second)
                fail("A backup selection profile contains duplicate mods.");
        }
        next["profiles"][name] = chosen;
    }
    if (next["profiles"].size() > 256) fail("Restoring these selection profiles would exceed the 256 profile limit.");
    if (nextLocal.size() > 4096) fail("The restored library would exceed the local catalog limit.");
    json payload = {{"schemaVersion", 1}, {"stateBinding", stateBinding()}, {"mods", nextLocal}};
    if (payload.dump().size() > 30u * 1024u * 1024u) fail("The restored library catalog exceeds its metadata limit.");
    auto key = approvalKey(true);
    writes.push_back(
        {"approved-local-catalog.json", {}, json{{"payload", payload}, {"mac", approvalMac(key, payload)}}.dump(2)});
    rememberActivations(next);
    writes.push_back({"state.json", {}, next.dump(2)});
    return writes;
}
