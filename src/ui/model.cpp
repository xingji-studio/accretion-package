#include "ap/ui.hpp"
namespace ap {
List<Choice> choices(const Installer& installer) {
    List<Choice> result;
    const char* groups[] = {"components", "shortcuts", "environment"};
    const char* categories[] = {"component", "shortcut", "environment"};
    for (unsigned group = 0; group < 3; ++group)
        for (const auto& item : installer.manifest.get(groups[group]).values) {
            Choice choice;
            choice.category = categories[group];
            choice.id = item.str("id");
            choice.required = item.boolean("required", false);
            choice.checked = choice.required || installer.options.chosen(choice.category, item);
            if (group == 0)
                choice.label = item.str("name", choice.id);
            else if (group == 1) {
                choice.label = "创建快捷方式：" + item.str("name", installer.manifest.get("app").str("name"));
                choice.value = item.str("application") + " → " +
                               targetExecutable(installer.manifest, item.str("application"));
            } else {
                choice.label =
                    Str(equal(os::name(), "xj380") ? (item.str("scope") == "machine" ? "系统环境变量："
                                                      : item.str("scope") == "user"  ? "用户环境变量："
                                                                                    : "应用环境变量：")
                                                   : "环境变量：") +
                    item.str("name");
                choice.value = expand(item.str("value"), installer.manifest, installer.options.directory,
                                      installer.options.machine);
            }
            result.add(move(choice));
        }
    return result;
}
Str prepareIcon(Installer& installer) {
    Str icon = installer.manifest.get("branding")
                   .str("installerIcon", installer.manifest.get("branding").str("applicationIcon"));
    if (icon.empty())
        return {};
    Str installed = join(installer.options.directory, icon);
    if (installer.options.uninstall)
        return os::info(installed).exists ? installed : Str();
    for (const auto& file : installer.manifest.get("files").values) {
        if (file.str("path") != icon)
            continue;
        if (file.integer("size", 0) > 4 * 1024 * 1024)
            return {};
        Str directory = join(installer.options.stateRoot.empty() ? join(os::stateHome(), "accretion/cache")
                                                                 : installer.options.stateRoot,
                             "icon-" + number(os::nonce()));
        if (!os::directories(directory))
            return {};
        installer.temporaryDirectories.add(directory);
        Str destination = join(directory, leaf(icon));
        if (installer.package.extract(file, destination))
            return destination;
        return {};
    }
    return {};
}
void choose(Installer& installer, const Choice& choice, bool checked) {
    installer.options.selected.set(choice.category + ":" + choice.id,
                                   Json::boolean(choice.required || checked));
}
} // namespace ap
