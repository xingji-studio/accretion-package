#include "ap/ui.hpp"
namespace ap {
namespace {
bool progress(void*, const Str& text, uint64_t current, uint64_t total) {
    os::output("  " + text + (total ? " [" + number(current * 100 / total) + "%]" : "") + "\n");
    return true;
}
bool prompt(const Str& label, Str& value) {
    os::output(label);
    return os::input(value);
}
} // namespace
Status tui(Installer& installer) {
    os::output("\nAccretion Package\n" + installer.manifest.get("app").str("name") + " " +
               installer.manifest.get("app").str("version") + "\n");
    Str input;
    if (installer.options.uninstall) {
        os::output("卸载目录：" + installer.options.directory + "\n修改过的文件和用户创建的文件将被保留。\n");
        if (!prompt("输入 yes 确认卸载，其他输入取消：", input) || input != "yes")
            return Status::fail("Uninstall cancelled");
        installer.progress = {nullptr, progress};
        return installer.uninstall();
    }
    if (!installer.license().empty() && !installer.options.accepted) {
        os::output("\n" + installer.license() + "\n\n");
        if (!prompt("输入 yes 接受许可协议，其他输入取消：", input) || input != "yes")
            return Status::fail("License was not accepted");
        installer.options.accepted = true;
    }
    if (installer.manifest.get("install").boolean("allowScopeChange", true)) {
        if (!prompt("安装范围 user/machine [" + Str(installer.options.machine ? "machine" : "user") + "]：",
                    input))
            return Status::fail("Installation cancelled");
        if (input == "machine" || input == "user") {
            installer.options.machine = input == "machine";
            installer.options.directory = installer.defaultDirectory(installer.options.machine);
        } else if (!input.empty())
            return Status::fail("Invalid installation scope");
    }
    if (installer.manifest.get("install").boolean("allowPathChange", true)) {
        if (!prompt("安装目录 [" + installer.options.directory + "]：", input))
            return Status::fail("Installation cancelled");
        if (!input.empty())
            installer.options.directory = slash(input);
    }
    for (;;) {
        auto items = choices(installer);
        os::output("\n安装目录：" + installer.options.directory + "\n");
        for (size_t i = 0; i < items.size(); ++i) {
            const auto& item = items[i];
            os::output(number(i + 1) + ". [" + Str(item.checked ? "x" : " ") + "] " + item.label +
                       (item.required ? "（必选）" : "") + "\n");
            if (!item.value.empty())
                os::output("     " + item.value + "\n");
        }
        os::output("L. [" + Str(installer.options.launch ? "x" : " ") + "] 安装完成后启动应用\n");
        if (!prompt("输入编号切换选项，L 切换启动，P 修改路径，Enter 开始，Q 取消：", input))
            return Status::fail("Installation cancelled");
        input = input.trim();
        if (input.lower() == "q")
            return Status::fail("Installation cancelled");
        if (input.lower() == "l") {
            installer.options.launch = !installer.options.launch;
            continue;
        }
        if (input.lower() == "p") {
            if (!installer.manifest.get("install").boolean("allowPathChange", true))
                continue;
            if (!prompt("安装目录：", input))
                return Status::fail("Installation cancelled");
            if (!input.empty())
                installer.options.directory = slash(input);
            continue;
        }
        if (!input.empty()) {
            uint64_t index;
            if (unsignedNumber(input, index) && index >= 1 && index <= items.size()) {
                choose(installer, items[size_t(index) - 1], !items[size_t(index) - 1].checked);
                continue;
            }
            os::output("无效选项。\n");
            continue;
        }
        Status ready = installer.check();
        if (!ready) {
            os::output(ready.message + "\n");
            continue;
        }
        installer.progress = {nullptr, progress};
        return installer.execute();
    }
}
} // namespace ap
