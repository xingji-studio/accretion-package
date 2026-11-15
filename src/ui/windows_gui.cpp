#include "ap/ui.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
namespace ap {
namespace {
List<wchar_t> wide(const Str& text) {
    int count =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), int(text.size()), nullptr, 0);
    List<wchar_t> result;
    auto* p = static_cast<wchar_t*>(ap_allocate((size_t(count) + 1) * sizeof(wchar_t)));
    if (count)
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), int(text.size()), p, count);
    for (int i = 0; i < count; ++i)
        result.add(p[i]);
    result.add(0);
    ap_deallocate(p);
    return result;
}
Str text(HWND handle) {
    int count = GetWindowTextLengthW(handle);
    auto* p = static_cast<wchar_t*>(ap_allocate((size_t(count) + 1) * sizeof(wchar_t)));
    GetWindowTextW(handle, p, count + 1);
    int bytes = WideCharToMultiByte(CP_UTF8, 0, p, count, nullptr, 0, nullptr, nullptr);
    Str out;
    out.resize(size_t(bytes));
    if (bytes)
        WideCharToMultiByte(CP_UTF8, 0, p, count, out.data(), bytes, nullptr, nullptr);
    ap_deallocate(p);
    return out;
}
void set(HWND window, const Str& value) {
    auto w = wide(value);
    SetWindowTextW(window, w.begin());
}
struct Dialog {
    Installer& installer;
    HWND window = nullptr, path = nullptr, browse = nullptr, scope = nullptr, checks = nullptr,
         accept = nullptr, license = nullptr, back = nullptr, next = nullptr, status = nullptr, bar = nullptr,
         launch = nullptr, title = nullptr, pathLabel = nullptr;
    HFONT font = nullptr;
    List<Choice> choices_;
    bool busy = false, cancel = false, done = false, licensePage = false;
    Status result = Status::fail("Installation cancelled");
    explicit Dialog(Installer& value) : installer(value) {}
    HWND control(const wchar_t* klass, const Str& label, DWORD style, int id, DWORD extended = 0) {
        auto w = wide(label);
        HWND child =
            CreateWindowExW(extended, klass, w.begin(), WS_CHILD | WS_VISIBLE | style, 0, 0, 100, 30, window,
                            reinterpret_cast<HMENU>(intptr_t(id)), GetModuleHandleW(nullptr), nullptr);
        if (child)
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return child;
    }
    void visible(HWND child, bool value) {
        if (child)
            ShowWindow(child, value ? SW_SHOW : SW_HIDE);
    }
    void showPage(bool licensing) {
        licensePage = licensing;
        visible(license, licensing);
        visible(accept, licensing);
        visible(path, !licensing);
        visible(pathLabel, !licensing);
        visible(scope, !licensing && !installer.options.uninstall);
        visible(browse, !licensing && !installer.options.uninstall &&
                            installer.manifest.get("install").boolean("allowPathChange", true));
        visible(checks, !licensing && !installer.options.uninstall);
        visible(launch, !licensing && !installer.options.uninstall);
        visible(bar, false);
        EnableWindow(back, !licensing && !installer.license().empty() && !installer.options.uninstall);
        EnableWindow(next, !licensing || installer.options.accepted);
        set(next, licensing ? "下一步" : installer.options.uninstall ? "卸载" : "安装");
        layout();
    }
    void layout() {
        RECT r;
        GetClientRect(window, &r);
        int w = r.right, h = r.bottom;
        MoveWindow(title, 24, 16, w - 48, 32, TRUE);
        MoveWindow(pathLabel, 24, 60, w - 48, 24, TRUE);
        MoveWindow(path, 24, 88, w - 154, 28, TRUE);
        if (browse)
            MoveWindow(browse, w - 120, 88, 96, 28, TRUE);
        MoveWindow(scope, 24, 126, w - 48, 180, TRUE);
        MoveWindow(checks, 24, 166, w - 48, h - 330, TRUE);
        MoveWindow(launch, 24, h - 155, w - 48, 26, TRUE);
        MoveWindow(license, 24, 60, w - 48, h - 240, TRUE);
        MoveWindow(accept, 24, h - 170, w - 48, 28, TRUE);
        MoveWindow(status, 24, h - 118, w - 48, 62, TRUE);
        MoveWindow(bar, 24, 100, w - 48, 25, TRUE);
        MoveWindow(back, 24, h - 44, 100, 30, TRUE);
        MoveWindow(next, w - 144, h - 44, 120, 30, TRUE);
        if (checks) {
            ListView_SetColumnWidth(checks, 0, (w - 55) * 45 / 100);
            ListView_SetColumnWidth(checks, 1, (w - 55) * 55 / 100);
        }
    }
    void collect() {
        installer.options.directory = slash(text(path));
        installer.options.machine = SendMessageW(scope, CB_GETCURSEL, 0, 0) == 1;
        for (size_t i = 0; i < choices_.size(); ++i)
            choose(installer, choices_[i], ListView_GetCheckState(checks, int(i)) != 0);
        installer.options.launch = SendMessageW(launch, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    void updateValues() {
        if (!checks || !path)
            return;
        installer.options.directory = slash(text(path));
        auto items = ap::choices(installer);
        for (size_t i = 0; i < items.size(); ++i) {
            auto value = wide(items[i].value);
            ListView_SetItemText(checks, int(i), 1, value.begin());
        }
    }
    static bool progress(void* context, const Str& value, uint64_t current, uint64_t total) {
        auto& d = *static_cast<Dialog*>(context);
        set(d.status, value);
        SendMessageW(d.bar, PBM_SETPOS, total ? WPARAM(current * 100 / total) : 0, 0);
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                d.cancel = true;
                continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return !d.cancel;
    }
    void forward() {
        if (busy)
            return;
        if (done) {
            DestroyWindow(window);
            return;
        }
        if (licensePage) {
            if (installer.options.accepted)
                showPage(false);
            return;
        }
        collect();
        Status ready = installer.check();
        if (!ready) {
            set(status, ready.message);
            return;
        }
        busy = true;
        EnableWindow(next, FALSE);
        EnableWindow(back, FALSE);
        visible(checks, false);
        visible(path, false);
        visible(scope, false);
        visible(browse, false);
        visible(launch, false);
        visible(pathLabel, false);
        visible(bar, true);
        installer.progress = {this, progress};
        result = installer.options.uninstall ? installer.uninstall() : installer.execute();
        busy = false;
        done = true;
        Str message = result ? (installer.options.uninstall ? "卸载完成。" : "安装完成。") : result.message;
        for (const auto& warning : installer.warnings)
            message.append("\r\n" + warning);
        if (result && !installer.options.uninstall && installer.options.launch) {
            Status started =
                installer.launch(installer.manifest.get("completion")
                                     .str("application", installer.manifest.get("applications").keys[0]));
            if (!started)
                message.append("\r\n" + started.message);
        }
        set(status, message);
        SendMessageW(bar, PBM_SETPOS, result ? 100 : 0, 0);
        set(next, "完成");
        EnableWindow(next, TRUE);
    }
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        Dialog* d = reinterpret_cast<Dialog*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            d = static_cast<Dialog*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(d));
            d->window = window;
        }
        if (!d)
            return DefWindowProcW(window, message, wparam, lparam);
        if (message == WM_COMMAND) {
            int id = LOWORD(wparam), event = HIWORD(wparam);
            if (id == 200)
                d->showPage(true);
            else if (id == 201)
                d->forward();
            else if (id == 110) {
                d->installer.options.accepted = SendMessageW(d->accept, BM_GETCHECK, 0, 0) == BST_CHECKED;
                EnableWindow(d->next, d->installer.options.accepted);
            } else if (id == 100 && event == EN_CHANGE && !d->busy)
                d->updateValues();
            else if (id == 101 && event == CBN_SELCHANGE) {
                bool machine = SendMessageW(d->scope, CB_GETCURSEL, 0, 0) == 1;
                Str current = slash(text(d->path)),
                    old = d->installer.defaultDirectory(d->installer.options.machine);
                d->installer.options.machine = machine;
                if (current == slash(old))
                    set(d->path, d->installer.defaultDirectory(machine));
                d->updateValues();
            } else if (id == 102) {
                BROWSEINFOW info{};
                info.hwndOwner = window;
                info.lpszTitle = L"选择安装目录";
                info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                PIDLIST_ABSOLUTE selected = SHBrowseForFolderW(&info);
                if (selected) {
                    wchar_t buffer[MAX_PATH];
                    if (SHGetPathFromIDListW(selected, buffer))
                        SetWindowTextW(d->path, buffer);
                    CoTaskMemFree(selected);
                }
            }
            return 0;
        }
        if (message == WM_NOTIFY) {
            auto* notice = reinterpret_cast<NMHDR*>(lparam);
            if (notice->idFrom == 103 && notice->code == LVN_ITEMCHANGING) {
                auto* change = reinterpret_cast<NMLISTVIEW*>(lparam);
                if (change->iItem >= 0 && size_t(change->iItem) < d->choices_.size() &&
                    d->choices_[size_t(change->iItem)].required && (change->uChanged & LVIF_STATE) &&
                    ((change->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(1)))
                    return TRUE;
            }
        }
        if (message == WM_SIZE && d->title) {
            d->layout();
            return 0;
        }
        if (message == WM_GETMINMAXINFO) {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            info->ptMinTrackSize = {640, 540};
            return 0;
        }
        if (message == WM_CLOSE) {
            if (d->busy) {
                d->cancel = true;
                set(d->status, "正在取消并回滚…");
                return 0;
            }
            DestroyWindow(window);
            return 0;
        }
        if (message == WM_DESTROY) {
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }
    Status run(bool smoke) {
        INITCOMMONCONTROLSEX common{sizeof(common), ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS};
        InitCommonControlsEx(&common);
        using DpiFunction = BOOL(WINAPI*)();
        auto dpi = reinterpret_cast<DpiFunction>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDPIAware"));
        if (dpi)
            dpi();
        font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        WNDCLASSW klass{};
        klass.lpfnWndProc = procedure;
        klass.hInstance = GetModuleHandleW(nullptr);
        klass.lpszClassName = L"AccretionInstaller";
        klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        klass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        klass.hIcon = LoadIconW(klass.hInstance, MAKEINTRESOURCEW(1));
        if (!klass.hIcon)
            klass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        RegisterClassW(&klass);
        Str caption =
            expand(installer.manifest.get("ui").str(
                       "title", installer.options.uninstall ? "卸载 ${app.name}" : "安装 ${app.name}"),
                   installer.manifest, installer.options.directory, installer.options.machine);
        auto wcaption = wide(caption);
        window = CreateWindowExW(0, klass.lpszClassName, wcaption.begin(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                 CW_USEDEFAULT, 860, 680, nullptr, nullptr, klass.hInstance, this);
        if (!window) {
            DeleteObject(font);
            return Status::fail("Cannot create Win32 installer window");
        }
        title = control(L"STATIC",
                        installer.manifest.get("app").str("name") + "  " +
                            installer.manifest.get("app").str("version"),
                        SS_LEFT, 1);
        pathLabel = control(
            L"STATIC", installer.options.uninstall ? "卸载目录（保留用户创建或修改过的文件）" : "安装目录",
            SS_LEFT, 2);
        path = control(L"EDIT", installer.options.directory,
                       ES_AUTOHSCROLL |
                           (installer.options.uninstall ||
                                    !installer.manifest.get("install").boolean("allowPathChange", true)
                                ? ES_READONLY
                                : 0),
                       100, WS_EX_CLIENTEDGE);
        SendMessageW(path, EM_SETLIMITTEXT, 2000, 0);
        browse = control(L"BUTTON", "浏览…", BS_PUSHBUTTON, 102);
        scope = control(L"COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, 101);
        SendMessageW(scope, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"当前用户"));
        SendMessageW(scope, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"所有用户（需要管理员权限）"));
        SendMessageW(scope, CB_SETCURSEL, installer.options.machine ? 1 : 0, 0);
        EnableWindow(scope, installer.manifest.get("install").boolean("allowScopeChange", true));
        checks =
            control(WC_LISTVIEWW, "", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 103, WS_EX_CLIENTEDGE);
        ListView_SetExtendedListViewStyle(checks,
                                          LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.cx = 340;
        column.pszText = const_cast<wchar_t*>(L"可选任务");
        ListView_InsertColumn(checks, 0, &column);
        column.pszText = const_cast<wchar_t*>(L"目标或变量值");
        ListView_InsertColumn(checks, 1, &column);
        choices_ = ap::choices(installer);
        for (size_t i = 0; i < choices_.size(); ++i) {
            auto label = wide(choices_[i].label + (choices_[i].required ? "（必选）" : ""));
            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = int(i);
            item.pszText = label.begin();
            ListView_InsertItem(checks, &item);
            ListView_SetCheckState(checks, int(i), choices_[i].checked);
            auto value = wide(choices_[i].value);
            ListView_SetItemText(checks, int(i), 1, value.begin());
        }
        launch = control(L"BUTTON", "安装完成后启动应用", BS_AUTOCHECKBOX, 111);
        SendMessageW(launch, BM_SETCHECK, installer.options.launch ? BST_CHECKED : BST_UNCHECKED, 0);
        license = control(L"EDIT", installer.license(),
                          ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL, 109, WS_EX_CLIENTEDGE);
        SendMessageW(license, EM_SETLIMITTEXT, 1024 * 1024, 0);
        accept = control(L"BUTTON", "我已阅读并接受许可协议", BS_AUTOCHECKBOX, 110);
        SendMessageW(accept, BM_SETCHECK, installer.options.accepted ? BST_CHECKED : BST_UNCHECKED, 0);
        status = control(L"EDIT", "", ES_MULTILINE | ES_READONLY | WS_VSCROLL, 112);
        bar = control(PROGRESS_CLASSW, "", 0, 113);
        back = control(L"BUTTON", "上一步", BS_PUSHBUTTON, 200);
        next = control(L"BUTTON", "安装", BS_DEFPUSHBUTTON, 201);
        showPage(!installer.license().empty() && !installer.options.uninstall);
        if (smoke) {
            for (size_t i = 0; i < choices_.size(); ++i)
                if ((ListView_GetCheckState(checks, int(i)) != 0) != choices_[i].checked)
                    return Status::fail("Win32 checkbox state mismatch");
            DestroyWindow(window);
            DeleteObject(font);
            return {};
        }
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);
        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
            if (!IsDialogMessageW(window, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        DeleteObject(font);
        return result;
    }
};
} // namespace
Status gui(Installer& installer, bool smoke) {
    Dialog dialog(installer);
    return dialog.run(smoke);
}
} // namespace ap
