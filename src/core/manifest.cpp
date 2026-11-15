#include "ap/package.hpp"
namespace ap {
bool validId(const Str& id) {
    if (id.empty() || id.size() > 120)
        return false;
    for (size_t i = 0; i < id.size(); ++i) {
        char c = id[i];
        bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!alnum && (!i || (c != '.' && c != '-' && c != '_')))
            return false;
    }
    return true;
}
bool validVariable(const Str& name) {
    if (name.empty() || name.size() > 120)
        return false;
    for (size_t i = 0; i < name.size(); ++i) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (i && c >= '0' && c <= '9')))
            return false;
    }
    return true;
}
namespace {
bool defined(const Json& object, const Str& key) {
    for (const auto& name : object.keys)
        if (name == key)
            return true;
    return false;
}
bool word(const char* list, const Str& value) {
    size_t start = 0;
    for (size_t i = 0;; ++i)
        if (!list[i] || list[i] == ' ') {
            if (Str(list + start, i - start) == value)
                return true;
            if (!list[i])
                return false;
            start = i + 1;
        }
}
Status object(const Json& value, const char* fields, const Str& label, bool optional = true) {
    if (optional && value.kind == Json::Null)
        return {};
    if (value.kind != Json::Object)
        return Status::fail(label + " must be an object");
    for (const auto& key : value.keys)
        if (!word(fields, key))
            return Status::fail("Unknown field " + label + "." + key);
    return {};
}
Status text(const Json& value, const Str& label, bool empty = true) {
    if (value.kind != Json::String || (!empty && value.text.empty()) || value.text.find('\0') != size_t(-1) ||
        !utf8(value.text))
        return Status::fail(label + " must be a valid UTF-8 string");
    return {};
}
Status stringField(const Json& parent, const char* key, const Str& label) {
    return defined(parent, key) ? text(parent.get(key), label + "." + key) : Status{};
}
Status booleanFields(const Json& value, const char* fields, const Str& label) {
    for (const auto& key : value.keys)
        if (word(fields, key) && value.get(key).kind != Json::Bool)
            return Status::fail(label + "." + key + " must be a boolean");
    return {};
}
Status array(const Json& value, const Str& label, size_t limit = 4096) {
    if (value.kind == Json::Null)
        return {};
    if (value.kind != Json::Array || value.values.size() > limit)
        return Status::fail(label + " must be an array within its size limit");
    return {};
}
Status strings(const Json& value, const Str& label) {
    AP_TRY(array(value, label));
    for (const auto& item : value.values)
        AP_TRY(text(item, label));
    return {};
}
Status inputs(const Json& value, const Str& label, bool required = false) {
    AP_TRY(object(value, "windows-x64 linux-x64 xj380-x64", label, !required));
    for (const auto& item : value.values)
        AP_TRY(text(item, label, false));
    return {};
}
Status unique(const Json& values, const Str& label) {
    AP_TRY(array(values, label, 1024));
    List<Str> ids;
    for (const auto& item : values.values) {
        if (item.kind != Json::Object || !validId(item.str("id")))
            return Status::fail(label + " needs a valid id on each entry");
        for (const auto& id : ids)
            if (id == item.str("id"))
                return Status::fail("Duplicate " + label + " id: " + id);
        ids.add(item.str("id"));
    }
    return {};
}
bool appExists(const Json& m, const Str& id) {
    return m.get("applications").get(id).kind == Json::Object;
}
int compare(const Str& a, const Str& b) {
    size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i)
        if (uint8_t(a[i]) != uint8_t(b[i]))
            return uint8_t(a[i]) < uint8_t(b[i]) ? -1 : 1;
    return a.size() == b.size() ? 0 : a.size() < b.size() ? -1 : 1;
}
void sort(List<Str>& values) {
    auto down = [&](size_t root, size_t count) {
        for (;;) {
            size_t child = root * 2 + 1;
            if (child >= count)
                return;
            if (child + 1 < count && compare(values[child], values[child + 1]) < 0)
                ++child;
            if (compare(values[root], values[child]) >= 0)
                return;
            values[root].swap(values[child]);
            root = child;
        }
    };
    for (size_t i = values.size() / 2; i > 0; --i)
        down(i - 1, values.size());
    for (size_t i = values.size(); i > 1; --i) {
        values[0].swap(values[i - 1]);
        down(0, i - 1);
    }
}
bool contains(const List<Str>& values, const Str& key) {
    size_t a = 0, b = values.size();
    while (a < b) {
        size_t middle = a + (b - a) / 2;
        int order = compare(values[middle], key);
        if (!order)
            return true;
        if (order < 0)
            a = middle + 1;
        else
            b = middle;
    }
    return false;
}
Status relative(const Str& path) {
    Str error;
    return relativePath(path, error) ? Status{} : Status::fail(error);
}
Status componentReference(const Json& m, const Json& entry) {
    if (!entry.has("component"))
        return {};
    Str id = entry.str("component");
    for (const auto& component : m.get("components").values)
        if (component.str("id") == id)
            return {};
    return Status::fail("Unknown component: " + id);
}
} // namespace
Status validateManifest(const Json& m, bool packaged) {
    AP_TRY(object(m,
                  "schemaVersion app inputs applications ui install branding completion environment "
                  "shortcuts components registry associations files compression target runtime",
                  "project", false));
    if (m.integer("schemaVersion", 0) != 1)
        return Status::fail("schemaVersion must be 1");
    const auto& app = m.get("app");
    AP_TRY(object(app, "id name version publisher description", "app", false));
    if (!validId(app.str("id")))
        return Status::fail("Invalid app.id");
    AP_TRY(text(app.get("name"), "app.name", false));
    AP_TRY(text(app.get("version"), "app.version", false));
    AP_TRY(stringField(app, "publisher", "app"));
    AP_TRY(stringField(app, "description", "app"));
    if (app.str("name").size() > 200 || app.str("version").size() > 80)
        return Status::fail("App name/version too long");
    const char* oneLine[] = {"name", "version", "publisher"};
    for (const char* key : oneLine) {
        Str value = app.str(key);
        for (size_t i = 0; i < value.size(); ++i)
            if (uint8_t(value[i]) < 32)
                return Status::fail("Control character in app metadata");
    }
    if (!packaged)
        AP_TRY(inputs(m.get("inputs"), "inputs", true));
    if (defined(m, "compression") && m.str("compression") != "lz4" && m.str("compression") != "store")
        return Status::fail("compression must be lz4 or store");
    const auto& apps = m.get("applications");
    if (apps.kind != Json::Object || apps.keys.empty() || apps.keys.size() > 1024)
        return Status::fail("At least one application is required");
    const char* platforms[] = {"windows", "linux", "xj380"};
    for (size_t i = 0; i < apps.keys.size(); ++i) {
        if (!validId(apps.keys[i]))
            return Status::fail("Invalid application id");
        const auto& entry = apps.values[i];
        AP_TRY(
            object(entry, "windows linux xj380 arguments workingDirectory terminal", "application", false));
        for (const char* platform : platforms)
            if (defined(entry, platform)) {
                AP_TRY(text(entry.get(platform), "executable", false));
                AP_TRY(relative(entry.str(platform)));
            }
        AP_TRY(strings(entry.get("arguments"), "application.arguments"));
        AP_TRY(stringField(entry, "workingDirectory", "application"));
        AP_TRY(booleanFields(entry, "terminal", "application"));
    }
    const auto& completion = m.get("completion");
    AP_TRY(object(completion, "application launchDefault", "completion"));
    AP_TRY(booleanFields(completion, "launchDefault", "completion"));
    if (!appExists(m, completion.str("application", apps.keys[0])))
        return Status::fail("Unknown completion application");
    const auto& ui = m.get("ui");
    AP_TRY(object(ui, "modes default language title", "ui"));
    AP_TRY(stringField(ui, "language", "ui"));
    AP_TRY(stringField(ui, "title", "ui"));
    Str defaultUi = ui.str("default", "gui");
    if (!word("gui tui auto", defaultUi))
        return Status::fail("Invalid UI default");
    if (defined(ui, "modes")) {
        AP_TRY(array(ui.get("modes"), "ui.modes", 2));
        if (ui.get("modes").values.empty())
            return Status::fail("ui.modes cannot be empty");
        bool gui = false, tui = false;
        for (const auto& mode : ui.get("modes").values) {
            if (mode.kind != Json::String || !word("gui tui", mode.text))
                return Status::fail("ui.modes supports gui and tui");
            if ((mode.text == "gui" && gui) || (mode.text == "tui" && tui))
                return Status::fail("Duplicate UI mode");
            if (mode.text == "gui")
                gui = true;
            else
                tui = true;
        }
        if ((defaultUi == "gui" && !gui) || (defaultUi == "tui" && !tui))
            return Status::fail("Default UI is disabled in ui.modes");
    }
    const auto& install = m.get("install");
    AP_TRY(object(install, "scope allowScopeChange allowPathChange paths", "install"));
    AP_TRY(booleanFields(install, "allowScopeChange allowPathChange", "install"));
    if (!word("user machine", install.str("scope", "user")))
        return Status::fail("Invalid installation scope");
    const auto& paths = install.get("paths");
    AP_TRY(object(paths, "windows linux xj380", "install.paths"));
    for (const auto& value : paths.values) {
        AP_TRY(object(value, "user machine", "install.paths"));
        for (const auto& item : value.values)
            AP_TRY(text(item, "install path", false));
    }
    const auto& branding = m.get("branding");
    AP_TRY(object(branding,
                  packaged ? "installerIcon applicationIcon licenseTexts licenseText"
                           : "installerIcon applicationIcon licenses licenseText",
                  "branding"));
    AP_TRY(stringField(branding, "installerIcon", "branding"));
    AP_TRY(stringField(branding, "applicationIcon", "branding"));
    AP_TRY(stringField(branding, "licenseText", "branding"));
    const auto& licenses = branding.get(packaged ? "licenseTexts" : "licenses");
    if (licenses.kind != Json::Null && licenses.kind != Json::Object)
        return Status::fail("License mappings must be objects");
    for (const auto& value : licenses.values)
        AP_TRY(text(value, "license text/path"));
    const char* categories[] = {"components", "environment", "shortcuts", "registry", "associations"};
    for (const char* category : categories)
        AP_TRY(unique(m.get(category), category));
    for (const auto& component : m.get("components").values) {
        AP_TRY(object(component, "id name inputs destination defaultChecked required requires", "component",
                      false));
        AP_TRY(booleanFields(component, "defaultChecked required", "component"));
        AP_TRY(stringField(component, "name", "component"));
        AP_TRY(inputs(component.get("inputs"), "component.inputs"));
        if (!component.str("destination").empty())
            AP_TRY(relative(component.str("destination")));
        AP_TRY(strings(component.get("requires"), "component.requires"));
        for (const auto& dependency : component.get("requires").values) {
            bool found = false;
            for (const auto& candidate : m.get("components").values)
                if (candidate.str("id") == dependency.text)
                    found = true;
            if (!found || dependency.text == component.str("id"))
                return Status::fail("Invalid component dependency");
        }
    }
    List<Str> names;
    for (const auto& entry : m.get("environment").values) {
        AP_TRY(object(entry, "id name value operation scope defaultChecked component", "environment", false));
        AP_TRY(booleanFields(entry, "defaultChecked", "environment"));
        if (!validVariable(entry.str("name")))
            return Status::fail("Invalid environment name");
        for (const auto& name : names)
            if (name.lower() == entry.str("name").lower())
                return Status::fail("Duplicate environment variable");
        names.add(entry.str("name"));
        AP_TRY(text(entry.get("value"), "environment.value"));
        if (!word("set setIfMissing appendUnique prependUnique", entry.str("operation", "setIfMissing")))
            return Status::fail("Invalid environment operation");
        if (!word("inherit application user machine", entry.str("scope", "inherit")))
            return Status::fail("Invalid environment scope");
        AP_TRY(componentReference(m, entry));
    }
    for (const auto& entry : m.get("shortcuts").values) {
        AP_TRY(object(entry, "id application name location arguments icon terminal defaultChecked component",
                      "shortcut", false));
        AP_TRY(booleanFields(entry, "terminal defaultChecked", "shortcut"));
        if (!appExists(m, entry.str("application")))
            return Status::fail("Unknown shortcut application");
        if (!word("desktop menu", entry.str("location", "desktop")))
            return Status::fail("Invalid shortcut location");
        AP_TRY(strings(entry.get("arguments"), "shortcut.arguments"));
        AP_TRY(stringField(entry, "name", "shortcut"));
        if (entry.has("icon"))
            AP_TRY(relative(entry.str("icon")));
        AP_TRY(componentReference(m, entry));
    }
    for (const auto& entry : m.get("registry").values) {
        AP_TRY(object(entry, "id key name type value scope", "registry", false));
        AP_TRY(text(entry.get("key"), "registry.key", false));
        AP_TRY(stringField(entry, "name", "registry"));
        if (!word("inherit user machine", entry.str("scope", "inherit")))
            return Status::fail("Invalid registry scope");
        Str type = entry.str("type", "string");
        if (type == "dword") {
            uint64_t value;
            if (entry.get("value").kind != Json::Number || !unsignedNumber(entry.get("value").text, value) ||
                value > UINT32_MAX)
                return Status::fail("DWORD registry value must be a u32");
        } else {
            if (!word("string expandString", type))
                return Status::fail("Unsupported registry type");
            AP_TRY(text(entry.get("value"), "registry.value"));
        }
    }
    for (const auto& entry : m.get("associations").values) {
        AP_TRY(object(entry, "id application description extensions", "association", false));
        if (!appExists(m, entry.str("application")))
            return Status::fail("Unknown association application");
        AP_TRY(stringField(entry, "description", "association"));
        AP_TRY(strings(entry.get("extensions"), "association.extensions"));
        if (entry.get("extensions").values.empty())
            return Status::fail("Association needs an extension");
        for (const auto& extension : entry.get("extensions").values) {
            if (!extension.text.starts(".") || extension.text.size() < 2)
                return Status::fail("Extension must start with a dot");
            for (size_t i = 1; i < extension.text.size(); ++i) {
                char c = extension.text[i];
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '_' || c == '-'))
                    return Status::fail("Invalid file extension");
            }
        }
    }
    if (!packaged && m.has("runtime"))
        return Status::fail("runtime is reserved for compiled packages");
    if (!packaged) {
        const auto& files = m.get("files");
        AP_TRY(object(files, "include exclude preserve dereferenceInternalLinks", "files"));
        AP_TRY(booleanFields(files, "dereferenceInternalLinks", "files"));
        AP_TRY(strings(files.get("include"), "files.include"));
        AP_TRY(strings(files.get("exclude"), "files.exclude"));
        AP_TRY(strings(files.get("preserve"), "files.preserve"));
        return {};
    }
    const auto& runtime = m.get("runtime");
    AP_TRY(object(runtime, "stub", "runtime"));
    if (runtime.has("stub"))
        AP_TRY(relative(runtime.str("stub")));
    Str target = m.str("target");
    if (!word("windows-x64 linux-x64 xj380-x64", target))
        return Status::fail("Invalid package target");
    const auto& files = m.get("files");
    if (files.kind != Json::Array || files.values.size() > 100000)
        return Status::fail("Invalid file index");
    List<Str> archiveNames;
    uint64_t total = 0;
    for (const auto& file : files.values) {
        AP_TRY(object(file, "path size sha256 mode chunks policy component", "file", false));
        AP_TRY(relative(file.str("path")));
        archiveNames.add(target == "windows-x64" ? file.str("path").lower() : file.str("path"));
        Str digest = file.str("sha256");
        if (digest.size() != 64)
            return Status::fail("Invalid file hash");
        for (size_t i = 0; i < digest.size(); ++i)
            if (!((digest[i] >= '0' && digest[i] <= '9') || (digest[i] >= 'a' && digest[i] <= 'f')))
                return Status::fail("Invalid file hash");
        uint64_t size = file.integer("size", UINT64_MAX), sum = 0;
        if (size == UINT64_MAX || size > UINT64_MAX - total || file.integer("mode", UINT64_MAX) > 0777)
            return Status::fail("Invalid file size/mode");
        total += size;
        if (file.get("chunks").kind != Json::Array)
            return Status::fail("Missing file chunks");
        for (const auto& chunk : file.get("chunks").values) {
            AP_TRY(object(chunk, "offset stored size codec", "chunk", false));
            uint64_t bytes = chunk.integer("size", UINT64_MAX), stored = chunk.integer("stored", UINT64_MAX);
            if (!bytes || bytes > ChunkSize || bytes > size - sum || !stored || stored > ChunkSize + 2048 ||
                chunk.integer("offset", UINT64_MAX) == UINT64_MAX || !word("store lz4", chunk.str("codec")))
                return Status::fail("Invalid compressed chunk");
            sum += bytes;
        }
        if (sum != size)
            return Status::fail("Chunk length mismatch");
        if (!word("replace preserve", file.str("policy", "replace")))
            return Status::fail("Invalid file policy");
        AP_TRY(componentReference(m, file));
    }
    sort(archiveNames);
    for (size_t i = 0; i < archiveNames.size(); ++i) {
        if (i && archiveNames[i] == archiveNames[i - 1])
            return Status::fail("Duplicate archive path");
        for (Str directory = parent(archiveNames[i]); !directory.empty(); directory = parent(directory))
            if (contains(archiveNames, directory))
                return Status::fail("Conflicting file/directory archive path");
    }
    Str platform = target.starts("windows") ? "windows" : target.starts("linux") ? "linux" : "xj380";
    for (const auto& application : apps.values) {
        Str binary = application.str(platform);
        if (binary.empty() || !contains(archiveNames, target == "windows-x64" ? binary.lower() : binary))
            return Status::fail("Application missing from payload: " + binary);
    }
    return {};
}
Str targetExecutable(const Json& m, const Str& id) {
    return m.get("applications").get(id).str(os::name());
}
Str expand(const Str& text, const Json& m, const Str& root, bool machine) {
    return text.replace("${install.dir}", root)
        .replace("${app.id}", m.get("app").str("id"))
        .replace("${app.name}", m.get("app").str("name"))
        .replace("${app.version}", m.get("app").str("version"))
        .replace("${user.home}", os::home())
        .replace("${paths.userPrograms}", os::programs(false))
        .replace("${paths.machinePrograms}", os::programs(true))
        .replace("${paths.desktop}", os::desktop(machine))
        .replace("${platform}", os::name())
        .replace("${arch}", "x64");
}
} // namespace ap
