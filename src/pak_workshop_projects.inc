// Included inside PakWorkshop::Impl. Project files are untrusted recipes: only
// immutable source bytes and typed, revalidated edits may reconstruct a draft.
static void projectFields(const json& value, std::initializer_list<const char*> keys) {
    if (!value.is_object() || value.size() != keys.size()) throw std::runtime_error("This project has unsupported or missing fields.");
    for (const auto key : keys) if (!value.contains(key)) throw std::runtime_error("This project is missing required information.");
}
static std::string projectHash(const json& value) {
    if (!value.is_string()) throw std::runtime_error("A project source checksum is invalid.");
    const auto hash = value.get<std::string>();
    if (hash.size() != 64 || hash.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw std::runtime_error("A project source checksum is invalid.");
    return hash;
}
static void projectNeed(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
std::uint64_t changedPropertyCount() const {
    std::uint64_t count = 0;
    for (const auto& pair : items) if (!pair.second.removed && pair.second.hasEdit && pair.second.selected != "omit") {
        const auto before = asset_properties::parse(originalContent(pair.second)),
                   after = asset_properties::parse(content(pair.second));
        projectNeed(before.supported && after.supported && before.fields.size() == after.fields.size(), "A changed project asset no longer has a supported property layout.");
        for (size_t i = 0; i < before.fields.size(); ++i) if (before.fields[i].value != after.fields[i].value) ++count;
    }
    return count;
}
struct ProjectSnapshot { json recipe; std::vector<workshop_projects::BlobView> blobs; };
ProjectSnapshot projectSnapshot() {
    ProjectSnapshot snapshot;
    json sourceRows = json::array(), familyRows = json::array(), edits = json::array(), merge = nullptr,
         addedFiles = json::array(), fileChanges = json::array();
    std::map<std::string, const Bytes*> unique;
    std::uint64_t sourceBytes = 0, baselineBytes = 0;
    auto add = [&](const Bytes& value, const std::string& hash = std::string{}) {
        const auto actual = hash.empty() ? pak_document::sha256Bytes(value) : hash;
        unique.emplace(actual, &value); return actual;
    };
    for (auto& source : sources) {
        json assets = json::array();
        for (const auto& asset : source.document.assets) {
            const auto key = normalizeAsset(asset.path);
            auto found = source.hashes.find(key);
            if (found == source.hashes.end()) found = source.hashes.emplace(key, pak_document::sha256Bytes(asset.bytes)).first;
            assets.push_back({{"path", asset.path}, {"sha256", add(asset.bytes, found->second)}});
            sourceBytes += asset.bytes.size();
        }
        sourceRows.push_back({{"id", source.id}, {"name", source.name}, {"kind", source.raw ? "asset" : "pak"}, {"assets", std::move(assets)}});
    }
    std::map<std::string, std::string> familyChoices;
    for (const auto& pair : items) if (!pair.second.added && !pair.second.selected.empty() && pair.second.selected != "omit") familyChoices[pair.second.family] = pair.second.selected;
    for (const auto& family : families) {
        if (family.second.empty()) continue;
        json selected = familyChoices.count(family.first) ? json(familyChoices.at(family.first)) : json();
        familyRows.push_back({{"path", family.first}, {"source", selected}});
    }
    asset_properties::Document mergeReference;
    if (merging) {
        const auto& value = items.at(merging->key);
        const auto result = compare(*merging, value);
        projectNeed(result.supported, "The current merge could not be validated before saving.");
        mergeReference = result.document; baselineBytes = merging->base.size();
        merge = {{"asset", merging->key}, {"baselineSha256", add(merging->base)}, {"left", merging->left},
                 {"right", merging->right}, {"choices", merging->choices}};
        if (!merging->versions.empty()) merge["versions"] = merging->versions;
    }
    for (const auto& pair : items) {
        const auto& value = pair.second;
        if (value.added) { addedFiles.push_back({{"path", value.originalPath}, {"sha256", add(*value.added)}}); sourceBytes += value.added->size(); }
        if (value.replacement) sourceBytes += value.replacement->size();
        if (value.removed || value.path != value.originalPath || value.replacement)
            fileChanges.push_back({{"asset", value.key}, {"path", value.path}, {"removed", value.removed},
                                   {"replacementSha256", value.replacement ? json(add(*value.replacement)) : json()}});
    }
    projectNeed(!sourceRows.empty() || !addedFiles.empty(), "Add a file before saving this empty package as a project.");
    projectNeed(sourceBytes <= SessionBudget, "This project exceeds its256MiB retained-source budget.");
    size_t metadataBytes = sourceRows.dump().size() + familyRows.dump().size() + merge.dump().size();
    for (const auto& pair : items) {
        const auto& value = pair.second;
        if (!value.hasEdit || value.selected.empty() || value.selected == "omit") continue;
        const auto& sourceBytesForAsset = originalContent(value);
        const auto& reference = merging && merging->key == value.key ? mergeReference.data : sourceBytesForAsset;
        const auto checked = asset_properties::threeWay(reference, content(value), reference);
        projectNeed(checked.supported, "A draft contains changes outside supported property values and cannot be saved as a project.");
        const auto before = asset_properties::parse(reference);
        const auto after = asset_properties::parse(content(value));
        json values = json::array();
        for (size_t i = 0; i < before.fields.size(); ++i) if (before.fields[i].value != after.fields[i].value) {
            json field{{"id", after.fields[i].id}, {"type", after.fields[i].type}, {"before", before.fields[i].value}, {"after", after.fields[i].value}};
            metadataBytes += field.dump().size();
            projectNeed(metadataBytes <= workshop_limits::MaxRecipeBytes, "This project exceeds the bounded recipe size. Save a smaller draft.");
            values.push_back(std::move(field));
        }
        if (!values.empty()) edits.push_back({{"asset", value.key}, {"sourceSha256", pak_document::sha256Bytes(sourceBytesForAsset)}, {"values", std::move(values)}});
    }
    const auto state = status().at("session");
    snapshot.recipe = {{"schemaVersion", 2}, {"sources", std::move(sourceRows)}, {"families", std::move(familyRows)},
        {"merge", std::move(merge)}, {"edits", std::move(edits)}, {"stats", {{"sourceCount", sources.size()},
            {"assetCount", state.at("assetCount")}, {"changedProperties", changedPropertyCount()},
            {"unresolvedCount", unresolved()}, {"sourceBytes", sourceBytes}, {"baselineBytes", baselineBytes}}},
        {"addedFiles", std::move(addedFiles)}, {"fileChanges", std::move(fileChanges)}};
    for (const auto& blob : unique) snapshot.blobs.push_back({blob.first, blob.second});
    return snapshot;
}
void replayProject(const workshop_projects::Loaded& loaded) {
    const auto& recipe = loaded.recipe;
    const bool version2 = recipe.value("schemaVersion", 0) == 2;
    if (version2) projectFields(recipe, {"schemaVersion", "sources", "families", "merge", "edits", "stats", "addedFiles", "fileChanges"});
    else projectFields(recipe, {"schemaVersion", "sources", "families", "merge", "edits", "stats"});
    projectNeed((version2 || recipe.at("schemaVersion") == 1) && recipe.at("sources").is_array() &&
                (version2 || !recipe.at("sources").empty()) && recipe.at("sources").size() <= workshop_limits::MaxSourceEntries,
                "This project has an unsupported source layout.");
    std::map<std::string, const Bytes*> blobs;
    for (const auto& blob : loaded.blobs) {
        projectNeed(blobs.emplace(projectHash(blob.sha256), &blob.bytes).second && pak_document::sha256Bytes(blob.bytes) == blob.sha256,
                    "A project source snapshot failed its checksum check.");
    }
    std::set<std::string> referenced;
    auto bytesFor = [&](const json& value) -> const Bytes& {
        const auto hash = projectHash(value); const auto found = blobs.find(hash);
        projectNeed(found != blobs.end(), "A saved source snapshot is missing. The current draft was preserved.");
        referenced.insert(hash); return *found->second;
    };
    std::vector<Source> opened; opened.reserve(recipe.at("sources").size()); std::uint64_t total = 0, baselineBytes = 0; std::size_t entries = 0;
    for (const auto& row : recipe.at("sources")) {
        projectFields(row, {"id", "name", "kind", "assets"});
        const auto id = "source-" + std::to_string(opened.size() + 1);
        projectNeed(row.at("id") == id && row.at("name").is_string() && row.at("kind").is_string() && row.at("assets").is_array() &&
                    !row.at("assets").empty() && row.at("assets").size() <= pak_document::MaxAssets,
                    "A saved source package is invalid.");
        projectNeed(row.at("assets").size() <= workshop_limits::MaxSourceEntries - entries,
                    "The saved project exceeds 12,288 source asset entries.");
        entries += row.at("assets").size();
        const auto name = row.at("name").get<std::string>(), kind = row.at("kind").get<std::string>();
        projectNeed(sourceName(fs::u8path(name)) == name && name.find_first_of("/\\:") == std::string::npos && (kind == "pak" || kind == "asset"),
                    "Project sources must use ordinary package names and supported kinds.");
        Source source; source.id = id; source.name = name; source.raw = kind == "asset";
        std::set<std::string> paths;
        for (const auto& asset : row.at("assets")) {
            projectFields(asset, {"path", "sha256"});
            projectNeed(asset.at("path").is_string(), "A saved asset path is invalid.");
            const auto path = asset.at("path").get<std::string>(), key = normalizeAsset(path);
            auto lower = path; std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            projectNeed(path.size() <= 4096 && lower == key && path.find('\\') == std::string::npos && paths.insert(key).second,
                        "A project contains duplicate, noncanonical or oversized asset paths.");
            const auto& bytes = bytesFor(asset.at("sha256"));
            projectNeed(bytes.size() <= pak_document::MaxAssetBytes && total <= SessionBudget && bytes.size() <= SessionBudget - total,
                        "The saved source data exceeds the workshop's memory limit.");
            total += bytes.size(); source.document.decodedBytes += bytes.size();
            source.document.assets.push_back({path, bytes}); source.hashes.emplace(key, projectHash(asset.at("sha256")));
        }
        if (source.raw) {
            projectNeed(source.document.assets.size() == 1 && source.document.assets.front().bytes.size() <= 16 * 1024 * 1024,
                        "This single-asset project has an unsupported layout.");
            const auto& asset = source.document.assets.front(); const auto key = normalizeAsset(asset.path);
            projectNeed(key.rfind("bendgame/content/", 0) == 0 && key.size() > 7 && key.substr(key.size() - 7) == ".uasset" &&
                        asset_properties::parse(asset.bytes).supported, "The saved exported asset no longer has a supported inline layout.");
        }
        opened.push_back(std::move(source));
    }
    replace(std::move(opened));
    projectNeed(recipe.at("families").is_array() && recipe.at("families").size() == families.size(), "The saved asset-family selection is incomplete.");
    std::set<std::string> seenFamilies;
    std::map<std::string, std::vector<Item*>> familyMembers;
    for (auto& pair : items) familyMembers[pair.second.family].push_back(&pair.second);
    for (const auto& row : recipe.at("families")) {
        projectFields(row, {"path", "source"});
        projectNeed(row.at("path").is_string(), "A saved asset family is invalid.");
        const auto family = row.at("path").get<std::string>();
        projectNeed(families.count(family) && seenFamilies.insert(family).second, "A saved asset family is missing or duplicated.");
        const auto& choice = row.at("source");
        projectNeed(choice.is_null() || choice.is_string() && families.at(family).count(choice.get<std::string>()), "A project names a source outside its asset family.");
        for (auto* member : familyMembers.at(family))
            member->selected = choice.is_null() ? std::string{} : member->versions.count(choice.get<std::string>()) ? choice.get<std::string>() : "omit";
    }
    if (version2) {
        projectNeed(recipe.at("addedFiles").is_array() && recipe.at("addedFiles").size() <= pak_document::MaxAssets &&
                    recipe.at("fileChanges").is_array() && recipe.at("fileChanges").size() <= pak_document::MaxAssets,
                    "The saved file operations exceed their supported metadata budget.");
        for (const auto& row : recipe.at("addedFiles")) {
            projectFields(row, {"path", "sha256"}); const auto& data = bytesFor(row.at("sha256"));
            projectNeed(data.size() <= pak_document::MaxAssetBytes && data.size() <= SessionBudget - total, "An added project file exceeds its memory budget.");
            addBytes(row.at("path").get<std::string>(), data, false); total += data.size();
        }
        std::set<std::string> changed;
        for (const auto& row : recipe.at("fileChanges")) {
            projectFields(row, {"asset", "path", "removed", "replacementSha256"});
            projectNeed(row.at("asset").is_string() && items.count(row.at("asset").get<std::string>()) &&
                        changed.insert(row.at("asset").get<std::string>()).second && row.at("path").is_string() && row.at("removed").is_boolean(),
                        "A saved file operation is invalid, missing or duplicated.");
            auto& value = items.at(row.at("asset").get<std::string>());
            const auto path = row.at("path").get<std::string>(); virtualFilePath(path);
            projectNeed(normalizeAsset(fs::u8path(value.originalPath).extension().u8string()) == normalizeAsset(fs::u8path(path).extension().u8string()),
                        "A saved rename changes the file type.");
            value.path = path; value.removed = row.at("removed").get<bool>();
            if (!row.at("replacementSha256").is_null()) {
                const auto& data = bytesFor(row.at("replacementSha256"));
                projectNeed(data.size() <= pak_document::MaxAssetBytes && data.size() <= SessionBudget - total && !value.selected.empty() && value.selected != "omit",
                            "A replacement project file exceeds its budget or has no selected source.");
                value.replacement = std::make_shared<const Bytes>(data); total += data.size();
            }
        }
        requireOutputPaths();
    }
    const auto& merge = recipe.at("merge");
    if (!merge.is_null()) {
        if (merge.contains("versions")) projectFields(merge, {"asset", "baselineSha256", "left", "right", "choices", "versions"});
        else projectFields(merge, {"asset", "baselineSha256", "left", "right", "choices"});
        const auto key = merge.at("asset").get<std::string>();
        projectNeed(items.count(key) && items.at(key).versions.size() >= 2 && key.size() > 7 && key.substr(key.size() - 7) == ".uasset", "The saved merge asset is not supported.");
        auto& value = items.at(key);
        projectNeed(!value.added && !value.removed && !value.replacement, "A saved property merge conflicts with an opaque file operation.");
        for (const auto& pair : items) projectNeed(pair.second.removed || pair.second.family != value.family || pair.first == key, "This saved merge uses unsupported companion files.");
        const auto left = merge.at("left").get<std::string>(), right = merge.at("right").get<std::string>();
        projectNeed(left != right && value.versions.count(left) && value.versions.count(right) && value.selected == left,
                    "The saved merge has incompatible source choices.");
        const auto& base = bytesFor(merge.at("baselineSha256")); baselineBytes = base.size();
        projectNeed(!base.empty() && base.size() <= 16 * 1024 * 1024 && value.versions.at(left)->bytes.size() <= 16 * 1024 * 1024 && value.versions.at(right)->bytes.size() <= 16 * 1024 * 1024,
                    "The saved property merge exceeds the interactive size limit.");
        merging = std::make_shared<Merge>(); merging->key = key; merging->base = base; merging->left = left; merging->right = right;
        if (merge.contains("versions")) {
            projectNeed(version2 && merge.at("versions").is_array() && merge.at("versions").size() == value.versions.size() && value.versions.size() > 2,
                        "A saved multi-source property comparison is incomplete.");
            std::set<std::string> seen;
            for (const auto& id : merge.at("versions")) {
                projectNeed(id.is_string() && value.versions.count(id.get<std::string>()) && seen.insert(id.get<std::string>()).second &&
                            value.versions.at(id.get<std::string>())->bytes.size() <= 16 * 1024 * 1024, "A saved comparison version is missing, oversized or duplicated.");
                merging->versions.push_back(id.get<std::string>());
            }
        } else projectNeed(value.versions.size() == 2, "A saved comparison omits source versions.");
        merging->choices = merge.at("choices"); const auto merged = compare(*merging, value);
        projectNeed(merged.supported, "The saved merge could not be revalidated against its original snapshot.");
        merging->conflicts = merged.conflicts;
        value.edited = merged.document.data; value.hasEdit = true;
    }
    projectNeed(recipe.at("edits").is_array() && recipe.at("edits").size() <= items.size(), "The saved property edit list is invalid.");
    std::set<std::string> editedAssets; size_t changedValues = 0;
    for (const auto& row : recipe.at("edits")) {
        projectFields(row, {"asset", "sourceSha256", "values"});
        const auto key = row.at("asset").get<std::string>();
        projectNeed(items.count(key) && editedAssets.insert(key).second && row.at("values").is_array() && !row.at("values").empty() && row.at("values").size() <= 8192,
                    "A saved property edit is missing or duplicated.");
        auto& value = items.at(key);
        projectNeed(!value.selected.empty() && value.selected != "omit" && content(value).size() <= 16 * 1024 * 1024 &&
                    pak_document::sha256Bytes(originalContent(value)) == projectHash(row.at("sourceSha256")),
                    "A saved property edit does not match its source snapshot.");
        auto parsed = asset_properties::parse(content(value)); projectNeed(parsed.supported, "A saved edited asset is no longer supported.");
        std::set<std::string> fields;
        std::map<std::string, const asset_properties::Field*> originals;
        for (const auto& field : parsed.fields) originals.emplace(field.id, &field);
        json replacements = json::object();
        for (const auto& field : row.at("values")) {
            projectFields(field, {"id", "type", "before", "after"});
            const auto id = field.at("id").get<std::string>();
            projectNeed(id.size() <= 128 && fields.insert(id).second && ++changedValues <= 32768, "The project has too many or duplicate property changes.");
            const auto found = originals.find(id);
            projectNeed(found != originals.end() && found->second->type == field.at("type") && found->second->value == field.at("before"),
                        "A saved property no longer matches its validated source value.");
            if (merging && merging->key == key)
                for (const auto& conflict : merging->conflicts) projectNeed(conflict.at("id") != id, "Unresolved merge values cannot be overridden by a project recipe.");
            replacements[id] = field.at("after");
        }
        asset_properties::setValues(parsed, replacements);
        value.edited = std::move(parsed.data); value.hasEdit = true;
    }
    projectNeed(retainedBytes() <= SessionBudget, "The reconstructed project exceeds its active memory budget.");
    projectNeed(referenced.size() == blobs.size(), "This project contains unreferenced source data.");
    const auto& stats = recipe.at("stats");
    projectFields(stats, {"sourceCount", "assetCount", "changedProperties", "unresolvedCount", "sourceBytes", "baselineBytes"});
    const auto state = status().at("session");
    projectNeed(stats.at("sourceCount") == sources.size() && stats.at("assetCount") == state.at("assetCount") &&
                stats.at("changedProperties") == changedPropertyCount() && stats.at("unresolvedCount") == unresolved() &&
                stats.at("sourceBytes") == total && stats.at("baselineBytes") == baselineBytes, "The saved project summary does not match its reconstructed draft.");
    project = loaded.summary; revision = savedRevision = 1; exportedRevision = 0; detachedProject = false;
    undo.clear(); undoBytes = 0; buildToken.clear(); output.clear(); outputOwner.reset();
}
void adoptProject(Impl& candidate) {
    using std::swap;
    clearExtract();
    swap(session, candidate.session); swap(revision, candidate.revision); swap(exportedRevision, candidate.exportedRevision);
    swap(savedRevision, candidate.savedRevision); swap(project, candidate.project); swap(detachedProject, candidate.detachedProject);
    swap(sources, candidate.sources); swap(items, candidate.items); swap(families, candidate.families); swap(merging, candidate.merging);
    swap(sourceFamilyIdentical, candidate.sourceFamilyIdentical); swap(sourceAnalysis, candidate.sourceAnalysis);
    swap(undo, candidate.undo); swap(undoBytes, candidate.undoBytes); swap(outputOwner, candidate.outputOwner); swap(output, candidate.output);
    swap(buildToken, candidate.buildToken); swap(buildSha, candidate.buildSha); swap(buildBytes, candidate.buildBytes);
    swap(buildTime, candidate.buildTime); swap(buildRevision, candidate.buildRevision);
}
json handleProject(const json& request) {
    const auto action = request.at("action").get<std::string>();
    std::set<std::string> allowed{"action", "requestId"};
    if (action == "workshopProjectSave") allowed.insert({"session", "revision", "name", "projectId"});
    else if (action == "workshopProjectOpen" || action == "workshopProjectDeleteReview") allowed.insert("id");
    else if (action == "workshopProjectDelete") allowed.insert({"token", "confirmed"});
    else projectNeed(action == "workshopProjectsList", "Choose a supported saved-project operation.");
    for (auto it = request.begin(); it != request.end(); ++it) projectNeed(allowed.count(it.key()) != 0, "Saved projects accept only native IDs, a name and the current draft revision.");
    if (action == "workshopProjectsList") return projectStore().list();
    if (action == "workshopProjectDeleteReview") {
        auto review = projectStore().reviewDelete(request.at("id").get<std::string>());
        auto selected = review; selected.erase("token"); selected.erase("warnings");
        return {{"token", review.at("token")}, {"project", selected}, {"warnings", review.value("warnings", json::array())}};
    }
    if (action == "workshopProjectDelete") {
        auto result = projectStore().deleteReviewed(request.at("token").get<std::string>(), request.value("confirmed", false));
        if (result.value("deleted", false) && project.is_object() && project.at("id") == result.at("id")) { project = nullptr; detachedProject = true; }
        result["session"] = status().at("session"); return result;
    }
    if (action == "workshopProjectSave") {
        check(request); const auto id = request.value("projectId", std::string{});
        projectNeed(id.empty() || project.is_object() && project.at("id") == id, "Use Save As to create a different project. The selected existing project was preserved.");
        const auto snapshot = projectSnapshot();
        const auto saved = projectStore().save(request.at("name").get<std::string>(), snapshot.recipe, snapshot.blobs, id,
                                             id.empty() ? std::string{} : project.at("generation").get<std::string>());
        project = saved; savedRevision = revision; detachedProject = false;
        auto result = status(); result["project"] = saved; return result;
    }
    try {
        const auto loaded = projectStore().load(request.at("id").get<std::string>());
        Impl candidate; candidate.replayProject(loaded); adoptProject(candidate);
    } catch (const std::exception& issue) {
        throw std::runtime_error(std::string("This project could not be reopened safely. Your current draft was preserved. ") + issue.what());
    }
    auto result = status(); result["project"] = project; return result;
}
