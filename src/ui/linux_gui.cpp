#include "ap/ui.hpp"
#include <dlfcn.h>
namespace ap {
namespace {
using Widget = void*;
using Callback = void (*)();
struct Gtk {
    void* library = nullptr;
#define GTK_FUNCTIONS(X)                                                                                     \
    X(int, gtk_init_check, (int*, char***))                                                                  \
    X(Widget, gtk_window_new, (int))                                                                         \
    X(void, gtk_window_set_title, (Widget, const char*))                                                     \
    X(void, gtk_window_set_default_size, (Widget, int, int))                                                 \
    X(void, gtk_window_set_position, (Widget, int))                                                          \
    X(int, gtk_window_set_icon_from_file, (Widget, const char*, void**))                                     \
    X(Widget, gtk_bin_get_child, (Widget))                                                                   \
    X(void, gtk_label_set_max_width_chars, (Widget, int))                                                    \
    X(Widget, gtk_box_new, (int, int))                                                                       \
    X(void, gtk_box_pack_start, (Widget, Widget, int, int, unsigned))                                        \
    X(void, gtk_container_add, (Widget, Widget))                                                             \
    X(void, gtk_container_set_border_width, (Widget, unsigned))                                              \
    X(Widget, gtk_label_new, (const char*))                                                                  \
    X(void, gtk_label_set_text, (Widget, const char*))                                                       \
    X(void, gtk_label_set_xalign, (Widget, float))                                                           \
    X(void, gtk_label_set_line_wrap, (Widget, int))                                                          \
    X(Widget, gtk_entry_new, ())                                                                             \
    X(void, gtk_entry_set_text, (Widget, const char*))                                                       \
    X(const char*, gtk_entry_get_text, (Widget))                                                             \
    X(void, gtk_editable_set_editable, (Widget, int))                                                        \
    X(Widget, gtk_button_new_with_label, (const char*))                                                      \
    X(void, gtk_button_set_label, (Widget, const char*))                                                     \
    X(Widget, gtk_check_button_new_with_label, (const char*))                                                \
    X(void, gtk_toggle_button_set_active, (Widget, int))                                                     \
    X(int, gtk_toggle_button_get_active, (Widget))                                                           \
    X(Widget, gtk_combo_box_text_new, ())                                                                    \
    X(void, gtk_combo_box_text_append_text, (Widget, const char*))                                           \
    X(void, gtk_combo_box_set_active, (Widget, int))                                                         \
    X(int, gtk_combo_box_get_active, (Widget))                                                               \
    X(Widget, gtk_scrolled_window_new, (void*, void*))                                                       \
    X(void, gtk_scrolled_window_set_policy, (Widget, int, int))                                              \
    X(Widget, gtk_text_view_new, ())                                                                         \
    X(Widget, gtk_text_view_get_buffer, (Widget))                                                            \
    X(void, gtk_text_buffer_set_text, (Widget, const char*, int))                                            \
    X(void, gtk_text_view_set_editable, (Widget, int))                                                       \
    X(void, gtk_text_view_set_wrap_mode, (Widget, int))                                                      \
    X(Widget, gtk_stack_new, ())                                                                             \
    X(Widget, gtk_stack_add_named, (Widget, Widget, const char*))                                            \
    X(void, gtk_stack_set_visible_child_name, (Widget, const char*))                                         \
    X(Widget, gtk_progress_bar_new, ())                                                                      \
    X(void, gtk_progress_bar_set_fraction, (Widget, double))                                                 \
    X(void, gtk_widget_set_sensitive, (Widget, int))                                                         \
    X(void, gtk_widget_show_all, (Widget))                                                                   \
    X(void, gtk_widget_destroy, (Widget))                                                                    \
    X(int, gtk_events_pending, ())                                                                           \
    X(int, gtk_main_iteration_do, (int))                                                                     \
    X(void, gtk_main, ())                                                                                    \
    X(void, gtk_main_quit, ())                                                                               \
    X(unsigned long, g_signal_connect_data, (Widget, const char*, Callback, void*, void*, int))              \
    X(Widget, gtk_file_chooser_dialog_new, (const char*, Widget, int, const char*, ...))                     \
    X(int, gtk_dialog_run, (Widget))                                                                         \
    X(char*, gtk_file_chooser_get_filename, (Widget))                                                        \
    X(void, g_free, (void*))
#define DECLARE(ret, name, args) ret(*name) args = nullptr;
    GTK_FUNCTIONS(DECLARE)
#undef DECLARE
    Status load() {
        library = dlopen("libgtk-3.so.0", RTLD_NOW | RTLD_LOCAL);
        if (!library)
            return Status::fail("GTK 3 runtime is unavailable; install GTK 3 or choose --ui tui");
#define LOAD(ret, name, args)                                                                                \
    name = reinterpret_cast<ret(*) args>(dlsym(library, #name));                                             \
    if (!name)                                                                                               \
        return Status::fail("Missing GTK symbol: " #name);
        GTK_FUNCTIONS(LOAD)
#undef LOAD
        return {};
    }
};
Gtk api;
struct Dialog {
    Installer& installer;
    Widget window = nullptr, stack = nullptr, path = nullptr, scope = nullptr, accept = nullptr,
           next = nullptr, back = nullptr, status = nullptr, bar = nullptr, launch = nullptr;
    List<Choice> choices_;
    List<Widget> checks;
    bool busy = false, cancel = false, done = false, licensePage = false, smoke = false;
    Status result = Status::fail("Installation cancelled");
    explicit Dialog(Installer& value) : installer(value) {}
    Widget label(const Str& text) {
        Widget widget = api.gtk_label_new(text.c_str());
        api.gtk_label_set_xalign(widget, 0);
        api.gtk_label_set_line_wrap(widget, 1);
        return widget;
    }
    Widget box(int orientation = 1, int spacing = 12) {
        return api.gtk_box_new(orientation, spacing);
    }
    void add(Widget parent, Widget child, bool expand = false) {
        api.gtk_box_pack_start(parent, child, expand, expand, 0);
    }
    void signal(Widget widget, const char* name, Callback function) {
        api.g_signal_connect_data(widget, name, function, this, nullptr, 0);
    }
    void options() {
        licensePage = false;
        api.gtk_stack_set_visible_child_name(stack, "options");
        api.gtk_button_set_label(next, installer.options.uninstall ? "卸载" : "安装");
        api.gtk_widget_set_sensitive(next, 1);
        api.gtk_widget_set_sensitive(back, !installer.license().empty() && !installer.options.uninstall);
    }
    void refreshLabels() {
        if (path)
            installer.options.directory = slash(api.gtk_entry_get_text(path));
        auto updated = ap::choices(installer);
        for (size_t i = 0; i < checks.size() && i < updated.size(); ++i) {
            choices_[i].value = updated[i].value;
            api.gtk_button_set_label(
                checks[i],
                (updated[i].label + (updated[i].value.empty() ? Str() : "\n    " + updated[i].value))
                    .c_str());
            Widget label = api.gtk_bin_get_child(checks[i]);
            if (label) {
                api.gtk_label_set_line_wrap(label, 1);
                api.gtk_label_set_max_width_chars(label, 70);
            }
        }
    }
    static void pathChanged(Widget, void* context) {
        static_cast<Dialog*>(context)->refreshLabels();
    }
    void collect() {
        if (path)
            installer.options.directory = slash(api.gtk_entry_get_text(path));
        if (scope)
            installer.options.machine = api.gtk_combo_box_get_active(scope) == 1;
        for (size_t i = 0; i < checks.size(); ++i)
            choose(installer, choices_[i], api.gtk_toggle_button_get_active(checks[i]) != 0);
        if (launch)
            installer.options.launch = api.gtk_toggle_button_get_active(launch) != 0;
    }
    static void accepted(Widget, void* context) {
        auto& d = *static_cast<Dialog*>(context);
        d.installer.options.accepted = api.gtk_toggle_button_get_active(d.accept) != 0;
        api.gtk_widget_set_sensitive(d.next, d.installer.options.accepted);
    }
    static void scopeChanged(Widget, void* context) {
        auto& d = *static_cast<Dialog*>(context);
        bool machine = api.gtk_combo_box_get_active(d.scope) == 1;
        Str old = d.installer.defaultDirectory(d.installer.options.machine);
        Str current = api.gtk_entry_get_text(d.path);
        d.installer.options.machine = machine;
        if (current == old)
            api.gtk_entry_set_text(d.path, d.installer.defaultDirectory(machine).c_str());
    }
    static void browse(Widget, void* context) {
        auto& d = *static_cast<Dialog*>(context);
        Widget dialog =
            api.gtk_file_chooser_dialog_new("选择安装目录", d.window, 2, "取消", -6, "选择", -3, nullptr);
        if (api.gtk_dialog_run(dialog) == -3) {
            char* value = api.gtk_file_chooser_get_filename(dialog);
            if (value) {
                api.gtk_entry_set_text(d.path, value);
                api.g_free(value);
            }
        }
        api.gtk_widget_destroy(dialog);
    }
    static void backward(Widget, void* context) {
        auto& d = *static_cast<Dialog*>(context);
        if (d.busy || d.done)
            return;
        d.licensePage = true;
        api.gtk_stack_set_visible_child_name(d.stack, "license");
        api.gtk_button_set_label(d.next, "下一步");
        api.gtk_widget_set_sensitive(d.next, d.installer.options.accepted);
        api.gtk_widget_set_sensitive(d.back, 0);
    }
    static bool progress(void* context, const Str& text, uint64_t current, uint64_t total) {
        auto& d = *static_cast<Dialog*>(context);
        api.gtk_label_set_text(d.status, text.c_str());
        api.gtk_progress_bar_set_fraction(d.bar, total ? double(current) / double(total) : 0);
        while (api.gtk_events_pending())
            api.gtk_main_iteration_do(0);
        return !d.cancel;
    }
    static int closing(Widget, void*, void* context) {
        auto& d = *static_cast<Dialog*>(context);
        if (d.busy) {
            d.cancel = true;
            return 1;
        }
        api.gtk_main_quit();
        return 0;
    }
    static void forward(Widget, void* context) {
        auto& d = *static_cast<Dialog*>(context);
        if (d.busy)
            return;
        if (d.done) {
            api.gtk_main_quit();
            return;
        }
        if (d.licensePage) {
            if (d.installer.options.accepted)
                d.options();
            return;
        }
        d.collect();
        Status ready = d.installer.check();
        if (!ready) {
            api.gtk_label_set_text(d.status, ready.message.c_str());
            return;
        }
        d.busy = true;
        api.gtk_widget_set_sensitive(d.next, 0);
        api.gtk_widget_set_sensitive(d.back, 0);
        api.gtk_stack_set_visible_child_name(d.stack, "progress");
        d.installer.progress = {&d, progress};
        d.result = d.installer.options.uninstall ? d.installer.uninstall() : d.installer.execute();
        d.busy = false;
        d.done = true;
        Str message =
            d.result ? (d.installer.options.uninstall ? "卸载完成。" : "安装完成。") : d.result.message;
        for (const auto& warning : d.installer.warnings)
            message.append("\n" + warning);
        api.gtk_label_set_text(d.status, message.c_str());
        api.gtk_button_set_label(d.next, "完成");
        api.gtk_widget_set_sensitive(d.next, 1);
        if (d.result && !d.installer.options.uninstall && d.installer.options.launch) {
            Str app = d.installer.manifest.get("completion")
                          .str("application", d.installer.manifest.get("applications").keys[0]);
            Status started = d.installer.launch(app);
            if (!started)
                api.gtk_label_set_text(d.status, (message + "\n" + started.message).c_str());
        }
    }
    Status run(bool smokeTest) {
        smoke = smokeTest;
        if (!api.gtk_init_check(nullptr, nullptr))
            return Status::fail("No graphical display is available; choose --ui tui");
        window = api.gtk_window_new(0);
        Str title =
            expand(installer.manifest.get("ui").str(
                       "title", installer.options.uninstall ? "卸载 ${app.name}" : "安装 ${app.name}"),
                   installer.manifest, installer.options.directory, installer.options.machine);
        api.gtk_window_set_title(window, title.c_str());
        api.gtk_window_set_default_size(window, 760, 620);
        api.gtk_window_set_position(window, 1);
        Str icon = prepareIcon(installer);
        if (!icon.empty())
            api.gtk_window_set_icon_from_file(window, icon.c_str(), nullptr);
        Widget main = box();
        api.gtk_container_set_border_width(main, 20);
        api.gtk_container_add(window, main);
        add(main, label(installer.manifest.get("app").str("name") + "  " +
                        installer.manifest.get("app").str("version")));
        stack = api.gtk_stack_new();
        add(main, stack, true);
        Widget optionsBox = box();
        api.gtk_stack_add_named(stack, optionsBox, "options");
        add(optionsBox, label("安装目录"));
        Widget pathRow = box(0, 8);
        path = api.gtk_entry_new();
        api.gtk_entry_set_text(path, installer.options.directory.c_str());
        api.gtk_editable_set_editable(path,
                                      !installer.options.uninstall &&
                                          installer.manifest.get("install").boolean("allowPathChange", true));
        add(pathRow, path, true);
        if (!installer.options.uninstall &&
            installer.manifest.get("install").boolean("allowPathChange", true)) {
            Widget button = api.gtk_button_new_with_label("浏览…");
            signal(button, "clicked", reinterpret_cast<Callback>(browse));
            add(pathRow, button);
        }
        add(optionsBox, pathRow);
        signal(path, "changed", reinterpret_cast<Callback>(pathChanged));
        if (!installer.options.uninstall) {
            scope = api.gtk_combo_box_text_new();
            api.gtk_combo_box_text_append_text(scope, "当前用户");
            api.gtk_combo_box_text_append_text(scope, "所有用户（需要管理员权限）");
            api.gtk_combo_box_set_active(scope, installer.options.machine ? 1 : 0);
            api.gtk_widget_set_sensitive(scope,
                                         installer.manifest.get("install").boolean("allowScopeChange", true));
            signal(scope, "changed", reinterpret_cast<Callback>(scopeChanged));
            add(optionsBox, scope);
            Widget scroll = api.gtk_scrolled_window_new(nullptr, nullptr);
            api.gtk_scrolled_window_set_policy(scroll, 2, 1);
            Widget list = box(1, 8);
            api.gtk_container_add(scroll, list);
            add(optionsBox, scroll, true);
            choices_ = ap::choices(installer);
            for (const auto& item : choices_) {
                Widget check = api.gtk_check_button_new_with_label(
                    (item.label + (item.value.empty() ? Str() : "\n    " + item.value)).c_str());
                api.gtk_toggle_button_set_active(check, item.checked);
                api.gtk_widget_set_sensitive(check, !item.required);
                checks.add(check);
                add(list, check);
            }
            refreshLabels();
            launch = api.gtk_check_button_new_with_label("安装完成后启动应用");
            api.gtk_toggle_button_set_active(launch, installer.options.launch);
            add(optionsBox, launch);
        } else
            add(optionsBox,
                label("卸载应用、快捷方式和本安装器创建的系统登记。保留用户创建或修改过的文件。"));
        Widget progressBox = box();
        bar = api.gtk_progress_bar_new();
        add(progressBox, bar);
        api.gtk_stack_add_named(stack, progressBox, "progress");
        status = label("");
        add(main, status);
        Widget footer = box(0, 8);
        back = api.gtk_button_new_with_label("上一步");
        next = api.gtk_button_new_with_label("安装");
        signal(back, "clicked", reinterpret_cast<Callback>(backward));
        signal(next, "clicked", reinterpret_cast<Callback>(forward));
        add(footer, back);
        add(footer, next);
        add(main, footer);
        if (!installer.license().empty() && !installer.options.uninstall) {
            Widget page = box(), scroll = api.gtk_scrolled_window_new(nullptr, nullptr),
                   text = api.gtk_text_view_new();
            api.gtk_text_view_set_editable(text, 0);
            api.gtk_text_view_set_wrap_mode(text, 3);
            api.gtk_text_buffer_set_text(api.gtk_text_view_get_buffer(text), installer.license().c_str(), -1);
            api.gtk_container_add(scroll, text);
            add(page, scroll, true);
            accept = api.gtk_check_button_new_with_label("我已阅读并接受许可协议");
            api.gtk_toggle_button_set_active(accept, installer.options.accepted);
            signal(accept, "toggled", reinterpret_cast<Callback>(accepted));
            add(page, accept);
            api.gtk_stack_add_named(stack, page, "license");
            backward(nullptr, this);
        } else
            options();
        signal(window, "delete-event", reinterpret_cast<Callback>(closing));
        if (smoke) {
            for (size_t i = 0; i < checks.size(); ++i)
                if ((api.gtk_toggle_button_get_active(checks[i]) != 0) != choices_[i].checked)
                    return Status::fail("GTK checkbox state mismatch");
            api.gtk_widget_destroy(window);
            return {};
        }
        api.gtk_widget_show_all(window);
        api.gtk_main();
        return result;
    }
};
} // namespace
Status gui(Installer& installer, bool smoke) {
    AP_TRY(api.load());
    Dialog dialog(installer);
    return dialog.run(smoke);
}
} // namespace ap
