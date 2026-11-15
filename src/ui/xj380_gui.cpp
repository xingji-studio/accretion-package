#include "ap/ui.hpp"
#include "ap/xj380.hpp"
namespace ap {
namespace {
struct Event {
    uint64_t type, h, l;
};
struct Slot {
    uint64_t sequence;
    Event value;
};
struct EventQueue {
    Slot slots[64]{};
    uint64_t written = 0, read = 0;
    void initialize() {
        for (uint64_t i = 0; i < 64; ++i)
            slots[i].sequence = i;
        written = read = 0;
    }
    void push(Event value) {
        uint64_t pos = __atomic_load_n(&written, __ATOMIC_RELAXED);
        Slot* slot;
        for (;;) {
            slot = &slots[pos % 64];
            int64_t difference = int64_t(__atomic_load_n(&slot->sequence, __ATOMIC_ACQUIRE) - pos);
            if (difference == 0) {
                if (__atomic_compare_exchange_n(&written, &pos, pos + 1, false, __ATOMIC_RELAXED,
                                                __ATOMIC_RELAXED))
                    break;
            } else if (difference < 0)
                return;
            else
                pos = __atomic_load_n(&written, __ATOMIC_RELAXED);
        }
        slot->value = value;
        __atomic_store_n(&slot->sequence, pos + 1, __ATOMIC_RELEASE);
    }
    bool pop(Event& value) {
        Slot& slot = slots[read % 64];
        if (int64_t(__atomic_load_n(&slot.sequence, __ATOMIC_ACQUIRE) - (read + 1)) != 0)
            return false;
        value = slot.value;
        __atomic_store_n(&slot.sequence, read + 64, __ATOMIC_RELEASE);
        ++read;
        return true;
    }
};
EventQueue queue;
void callback(uint64_t type, uint64_t h, uint64_t l) {
    if (type == 0 || type == 2 || type == 5 || type == 6 || type == 7 || type == 8)
        queue.push({type, h, l});
}
List<Str> wrap(const Str& text, size_t width) {
    List<Str> result;
    Str line;
    size_t units = 0;
    for (size_t i = 0; i < text.size();) {
        uint8_t c = text[i];
        if (c == '\r') {
            ++i;
            continue;
        }
        if (c == '\n') {
            result.add(move(line));
            line = Str();
            units = 0;
            ++i;
            continue;
        }
        size_t bytes = c < 128 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : 4;
        if (i + bytes > text.size())
            break;
        size_t next = c < 128 ? 1 : 2;
        if (units + next > width && !line.empty()) {
            result.add(move(line));
            line = Str();
            units = 0;
        }
        line.append(text.c_str() + i, bytes);
        i += bytes;
        units += next;
    }
    if (!line.empty() || result.empty())
        result.add(move(line));
    return result;
}
struct Dialog {
    Installer& installer;
    XHandle window = 0;
    uint64_t pathInput = 0;
    bool licensing = false, done = false, busy = false, cancel = false, closed = false, inputFocus = false;
    size_t page = 0, focus = 0;
    List<Choice> items;
    List<Str> licenseLines;
    Str status;
    Status result = Status::fail("Installation cancelled");
    explicit Dialog(Installer& value) : installer(value) {}
    void text(unsigned x, unsigned y, const Str& value, uint32_t size = 17, uint32_t color = 0x202939) {
        xapi_DrawText(window, x, y, const_cast<char*>(value.c_str()), size, color);
    }
    void rect(unsigned x, unsigned y, unsigned w, unsigned h, uint32_t color, bool fill = true) {
        xapi_DrawRect(window, x, y, x + w, y + h, color, fill);
    }
    void checkbox(unsigned x, unsigned y, bool checked, const Str& label, bool enabled = true) {
        rect(x, y, 19, 19, enabled ? 0x325cba : 0xaaaaaa, false);
        if (checked) {
            xapi_DrawLine(window, x + 3, y + 10, x + 8, y + 15, 0x325cba);
            xapi_DrawLine(window, x + 8, y + 15, x + 16, y + 4, 0x325cba);
        }
        text(x + 30, y + 17, label, 17, enabled ? 0x202939 : 0x888888);
    }
    void button(unsigned x, unsigned y, unsigned w, const Str& label, bool enabled = true) {
        rect(x, y, w, 34, enabled ? 0xe7eefc : 0xeeeeee);
        text(x + 12, y + 24, label, 17, enabled ? 0x214da6 : 0x999999);
    }
    void input() {
        if (pathInput) {
            char buffer[256]{};
            xapi_GetTextInputBox(pathInput, buffer);
            buffer[255] = 0;
            installer.options.directory = slash(buffer);
        }
    }
    void rebuildInput() {
        if (pathInput)
            xapi_DeleteTextInputBox(pathInput);
        pathInput = 0;
        if (!licensing && !done && !busy && !installer.options.uninstall) {
            pathInput = xapi_PutTextInputBox(window, 28, 88, 838,
                                             const_cast<char*>(installer.options.directory.c_str()));
        }
    }
    void draw() {
        if (pathInput) {
            input();
            xapi_DeleteTextInputBox(pathInput);
            pathInput = 0;
        }
        rect(0, 0, 900, 700, 0xffffff);
        text(28, 35,
             installer.manifest.get("app").str("name") + "  " + installer.manifest.get("app").str("version"),
             24);
        text(28, 60, "XJ380 · 原生 XAPI", 15, 0x666666);
        if (done || busy) {
            auto lines = wrap(status, 88);
            unsigned y = 128;
            for (size_t i = 0; i < lines.size() && i < 18; ++i, y += 24)
                text(28, y, lines[i]);
            button(738, 640, 128, done ? "完成" : "取消");
        } else if (licensing) {
            size_t start = page * 18;
            for (size_t i = start; i < licenseLines.size() && i < start + 18; ++i)
                text(28, 100 + unsigned(i - start) * 24, licenseLines[i], 16);
            checkbox(28, 558, installer.options.accepted, "我已阅读并接受许可协议");
            text(28, 615, "第 " + number(page + 1) + " / " + number((licenseLines.size() + 17) / 18) + " 页",
                 15, 0x666666);
            button(510, 594, 90, "上一页", page > 0);
            button(612, 594, 90, "下一页", (page + 1) * 18 < licenseLines.size());
            button(738, 640, 128, "下一步", installer.options.accepted);
        } else {
            text(28, 82, installer.options.uninstall ? "卸载目录" : "安装目录", 16);
            if (installer.options.uninstall)
                text(28, 114, installer.options.directory, 16);
            checkbox(28, 132, installer.options.machine, "为所有用户安装（需要权限）",
                     !installer.options.uninstall &&
                         installer.manifest.get("install").boolean("allowScopeChange", true));
            items = choices(installer);
            size_t start = page * 6;
            for (size_t i = start; i < items.size() && i < start + 6; ++i) {
                unsigned y = 181 + unsigned(i - start) * 57;
                if (i == focus)
                    rect(23, y - 3, 853, 52, 0xf0f4fc);
                checkbox(30, y, items[i].checked, items[i].label,
                         !items[i].required && !installer.options.uninstall);
                if (!items[i].value.empty()) {
                    auto lines = wrap(items[i].value, 96);
                    text(60, y + 40, lines[0], 14, 0x626b7a);
                }
            }
            checkbox(28, 552, installer.options.launch, "安装完成后启动应用", !installer.options.uninstall);
            text(28, 614,
                 "选项 " + number(page + 1) + " / " + number(items.empty() ? 1 : (items.size() + 5) / 6), 15,
                 0x666666);
            button(510, 594, 90, "上一页", page > 0);
            button(612, 594, 90, "下一页", (page + 1) * 6 < items.size());
            if (!installer.license().empty() && !installer.options.uninstall)
                button(28, 640, 100, "上一步");
            button(738, 640, 128, installer.options.uninstall ? "卸载" : "安装");
        }
        if (!done && !busy) {
            button(620, 640, 100, "取消");
            if (!status.empty())
                text(28, 588, wrap(status, 90)[0], 14, 0xa13e26);
        }
        if (!licensing && !done && !busy && !installer.options.uninstall)
            pathInput = xapi_PutTextInputBox(window, 28, 88, 838,
                                             const_cast<char*>(installer.options.directory.c_str()));
        xapi_RefreshWindow(window);
    }
    void forward() {
        if (done) {
            closed = true;
            return;
        }
        if (busy) {
            cancel = true;
            return;
        }
        if (licensing) {
            if (installer.options.accepted) {
                licensing = false;
                page = 0;
                rebuildInput();
                draw();
            }
            return;
        }
        input();
        Status ready = installer.check();
        if (!ready) {
            status = ready.message;
            draw();
            return;
        }
        if (pathInput) {
            xapi_DeleteTextInputBox(pathInput);
            pathInput = 0;
        }
        busy = true;
        status = installer.options.uninstall ? "正在卸载…" : "正在安装…";
        draw();
        installer.progress = {this, progress};
        result = installer.options.uninstall ? installer.uninstall() : installer.execute();
        busy = false;
        done = true;
        status = result ? (installer.options.uninstall ? "卸载完成。" : "安装完成。") : result.message;
        for (const auto& warning : installer.warnings)
            status.append("\n" + warning);
        if (result && !installer.options.uninstall && installer.options.launch) {
            Status launch =
                installer.launch(installer.manifest.get("completion")
                                     .str("application", installer.manifest.get("applications").keys[0]));
            if (!launch)
                status.append("\n" + launch.message);
        }
        draw();
    }
    void event(const Event& event) {
        if (event.type == 8) {
            draw();
            return;
        }
        if (event.type == 2) {
            unsigned x = unsigned(event.h), y = unsigned(event.l);
            if (busy) {
                if (y >= 640 && x >= 738)
                    cancel = true;
                return;
            }
            if (done) {
                if (y >= 640 && x >= 738)
                    closed = true;
                return;
            }
            if (y >= 640) {
                if (x >= 738) {
                    forward();
                    return;
                }
                if (x >= 620) {
                    closed = true;
                    return;
                }
                if (x <= 140 && !installer.license().empty() && !installer.options.uninstall) {
                    input();
                    licensing = true;
                    page = 0;
                    rebuildInput();
                    draw();
                }
                return;
            }
            if (y >= 594 && y < 630) {
                if (x >= 510 && x <= 600 && page) {
                    input();
                    --page;
                    draw();
                } else if (x >= 612 && x <= 702) {
                    size_t count = licensing ? (licenseLines.size() + 17) / 18 : (items.size() + 5) / 6;
                    if (page + 1 < count) {
                        input();
                        ++page;
                        draw();
                    }
                }
                return;
            }
            if (licensing) {
                if (y >= 550 && y < 584) {
                    installer.options.accepted = !installer.options.accepted;
                    draw();
                }
                return;
            }
            if (y >= 88 && y < 125) {
                inputFocus = true;
                return;
            }
            inputFocus = false;
            if (y >= 132 && y <= 160 && !installer.options.uninstall &&
                installer.manifest.get("install").boolean("allowScopeChange", true)) {
                input();
                Str previous = installer.defaultDirectory(installer.options.machine);
                installer.options.machine = !installer.options.machine;
                if (installer.options.directory == previous)
                    installer.options.directory = installer.defaultDirectory(installer.options.machine);
                rebuildInput();
                draw();
                return;
            }
            if (y >= 552 && y < 580 && !installer.options.uninstall) {
                installer.options.launch = !installer.options.launch;
                draw();
                return;
            }
            if (y >= 178 && y < 525 && !installer.options.uninstall) {
                size_t at = page * 6 + (y - 178) / 57;
                if (at < items.size()) {
                    input();
                    focus = at;
                    choose(installer, items[at], !items[at].checked);
                    draw();
                }
            }
            return;
        }
        if (event.type == 7) {
            uint64_t key = event.h;
            if (key == 128) {
                if (busy)
                    cancel = true;
                else
                    closed = true;
                return;
            }
            if (busy || done)
                return;
            if (inputFocus) {
                if (key == 130 || key == 131) {
                    input();
                    inputFocus = false;
                    draw();
                }
                return;
            }
            if (key == 131) {
                forward();
                return;
            }
            if (key == 153 || key == 158) {
                size_t count = licensing ? (licenseLines.size() + 17) / 18 : (items.size() + 5) / 6;
                if (key == 153 && page)
                    --page;
                else if (key == 158 && page + 1 < count)
                    ++page;
                draw();
                return;
            }
            if (licensing && key == 136) {
                installer.options.accepted = !installer.options.accepted;
                draw();
                return;
            }
            if (!licensing && !items.empty()) {
                if (key == 152 && focus)
                    --focus;
                else if (key == 157 && focus + 1 < items.size())
                    ++focus;
                else if (key == 136 && !installer.options.uninstall)
                    choose(installer, items[focus], !items[focus].checked);
                page = focus / 6;
                draw();
            }
        }
    }
    static bool progress(void* context, const Str& text, uint64_t current, uint64_t total) {
        auto& d = *static_cast<Dialog*>(context);
        d.status = text + (total ? " [" + number(current * 100 / total) + "%]" : "");
        Event e;
        while (queue.pop(e))
            d.event(e);
        d.draw();
        return !d.cancel;
    }
    Status run(bool smoke) {
        if (installer.options.directory.size() >= 256)
            return Status::fail(
                "Native XAPI text fields support 255 UTF-8 bytes; use --ui tui for a longer directory");
        queue.initialize();
        Str title =
            expand(installer.manifest.get("ui").str(
                       "title", installer.options.uninstall ? "卸载 ${app.name}" : "安装 ${app.name}"),
                   installer.manifest, installer.options.directory, installer.options.machine);
        XWindow request{900, 700, const_cast<char*>(title.c_str()), 0};
        xapi_CreateWindow(&window, &request);
        if (!window || int64_t(window) < 0)
            return Status::fail("XAPI GUI is unavailable on this system image");
        Str icon = prepareIcon(installer);
        if (!icon.empty())
            xapi_SetIcon(window, const_cast<char*>(icon.c_str()));
        SetMsgPrcor(window, callback);
        licensing = !installer.license().empty() && !installer.options.uninstall;
        licenseLines = wrap(installer.license(), 88);
        items = choices(installer);
        rebuildInput();
        draw();
        if (smoke) {
            xapi_CloseWindow(window);
            return {};
        }
        while (!closed) {
            Event event;
            bool any = false;
            while (queue.pop(event)) {
                any = true;
                this->event(event);
            }
            if (!any)
                xapi_Sleep(10);
        }
        if (pathInput)
            xapi_DeleteTextInputBox(pathInput);
        xapi_CloseWindow(window);
        return result;
    }
};
} // namespace
Status gui(Installer& installer, bool smoke) {
    Dialog dialog(installer);
    return dialog.run(smoke);
}
} // namespace ap
