# Accretion Package

将应用目录打包为 Windows、Linux、XJ380 的原生 GUI / TUI 安装程序。当前实现面向 **x86-64**，包括打包 CLI、终端配置向导、可选的 Qt 图形工作台、安装器、启动器和卸载器。

| 目标 | GUI | TUI | 单文件输出 | 系统集成 |
| --- | --- | --- | --- | --- |
| Windows | Win32 原生窗口 | 原生控制台 | PE `.exe` | `.lnk`、卸载列表、注册表、环境变量、打开方式 |
| Linux | 原生 GTK 3 | 终端向导 | ELF `.run` | `.desktop`、XDG MIME、Shell / environment.d 环境配置 |
| XJ380 | 原生 XAPI 窗口、绘图、文本框和事件 | 原生 XAPI 输入输出 | ELF `.elf` / EPF `.epf` | 原生 ELF 快捷启动器、应用环境变量；系统集成适配器已预留 |

XJ380 运行时代码直接使用 XAPI 1.4 ABI，不依赖 Linux 系统调用、POSIX 兼容层、libc、libstdc++、Qt 或 GTK。GUI / TUI 共用安装引擎、清单与事务日志。

## 快速体验

一键编译与打包工具本身：

```bash
make build
make package
# 指定宿主、图形前端及全部安装器目标
AP_PLATFORM=linux AP_FRONTEND=gui AP_RUNTIME_PLATFORMS=all make package
```

Windows 使用 `.\build.cmd`、`.\package.cmd`，或通用的 `python tools/project.py build|package`。发布目录默认位于 `dist/accretion-package-<平台>-x64/`，包含前后端、安装器模板、运行库、启动入口和依赖检查脚本，可整体移动后独立使用。[完整构建与环境变量说明](docs/building.md)。

下面是保留的轻量构建与应用安装包示例：

在 Linux / WSL 构建全部目标需要 Python 3.10+、GCC / G++、Clang、GNU binutils、MinGW-w64 的 C 编译器及 Windows 头文件。源码已包含 LZ4 和官方 elf2epf 转换库，构建过程不下载依赖。

```bash
# 生成打包器与六个 GUI / TUI 安装器模板
python3 tools/build.py --target all

# 构建三个平台的原生示例应用
python3 tools/build_demo.py --target all

# 验证示例工程并打包
./build/apack validate --project examples/project.json
./build/apack build --project examples/project.json --target windows-x64 --ui gui
./build/apack build --project examples/project.json --target linux-x64 --ui tui
./build/apack build --project examples/project.json --target xj380-x64 --ui gui --format epf
```

输出位于 `out/`。每个目标必须提供该系统可运行的应用二进制；打包器不会把 Windows 应用转换为 Linux 或 XJ380 应用。

## 构建

### CMake 原生构建

```bash
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --config Release -j 4
ctest --test-dir build/native -C Release --output-on-failure
```

Windows 推荐 Visual Studio 2022 的 x64 Native Tools 命令行或 Developer PowerShell，使用相同 CMake 命令。多配置生成器的 `apack.exe` 在 `build/native/Release/`，模板在 `build/native/Release/ap-stubs/`。**使用 `cmake --install` 后目录会统一为 `bin/apack` 和 `bin/ap-stubs/`**；未安装时也可向打包命令传入 `--stub`。

```powershell
cmake --install build/native --config Release --prefix build/dist
.\build\dist\bin\apack.exe --help
```

发现 Qt 6 Widgets 开发包时，CMake 同时生成 `apack-workbench`。设置 `CMAKE_PREFIX_PATH` 可指定 Qt；`-DAP_BUILD_WORKBENCH=OFF` 仅关闭图形工作台。安装器 GUI 仍然可用。Linux GUI 运行时需要 GTK 3；TUI 不需要图形环境。

```bash
# XJ380 原生 ELF 模板交叉构建；不构建宿主打包器
cmake -S . -B build/xj380 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/xj380-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build/xj380 -j 4
```

`tools/build.py --target linux|windows|xj380` 可以分别生成模板。该脚本的宿主打包器构建路径面向 Linux / WSL，Windows 宿主请使用 CMake。模板默认放在打包器旁的 `ap-stubs/`；交叉生成某一系统的安装程序只需要该系统的模板，无需在该系统上运行打包器。

### 配置界面

运行 `apack-workbench`：填写名称、产品 ID、版本、各目标的输入目录、图标、许可协议、应用入口、环境变量和快捷方式。环境变量与快捷方式各有独立的“默认勾选”复选框。高级 JSON 页支持完整配置，修改后点击“应用 JSON 到表单”，保存后构建。模板和 `apack` 应与工作台一起安装。

终端配置向导：

```bash
./build/apack wizard --output project.json
# 或直接生成工程
./build/apack init --input ./my-app --main bin/my-app \
  --target linux-x64 --name "我的应用" --id com.example.myapp \
  --output project.json
```

## 工程配置

完整示例见 [examples/project.json](examples/project.json)，字段约束见 [schema/project.schema.json](schema/project.schema.json)，进一步说明见 [docs/configuration.md](docs/configuration.md)。输入目录、图标和协议的相对路径以工程 JSON 所在目录为基准。

```json
{
  "schemaVersion": 1,
  "app": { "id": "com.example.toolbox", "name": "我的工具箱", "version": "1.0.0" },
  "inputs": { "linux-x64": "./payload" },
  "applications": {
    "main": { "linux": "bin/toolbox" },
    "cli": { "linux": "bin/toolbox-cli", "terminal": true }
  },
  "ui": { "default": "gui", "modes": ["gui", "tui"] },
  "install": {
    "scope": "user", "allowPathChange": true, "allowScopeChange": true,
    "paths": { "linux": { "user": "${user.home}/Apps/${app.id}", "machine": "/opt/${app.id}" } }
  },
  "completion": { "application": "main", "launchDefault": false },
  "environment": [
    { "id": "home", "name": "TOOLBOX_HOME", "value": "${install.dir}", "defaultChecked": true },
    { "id": "path", "name": "PATH", "value": "${install.dir}/bin", "operation": "appendUnique", "defaultChecked": true }
  ],
  "shortcuts": [
    { "id": "desktop-cli", "name": "工具箱命令行", "application": "cli", "location": "desktop", "defaultChecked": true }
  ]
}
```

安装完成后启动哪个程序由 `completion.application` 控制；每个快捷方式独立选择 `application`，不受完成页默认程序影响。每个应用支持自己的参数、工作目录和终端选项。

默认安装位置：

| 平台 | 当前用户 | 全系统 |
| --- | --- | --- |
| Windows | `%LOCALAPPDATA%/Programs/<名称>` | `%ProgramFiles%/<名称>` |
| Linux | `~/.local/opt/<产品 ID>` | `/opt/<产品 ID>` |
| XJ380 | `/users/<用户名>/Applications/<产品 ID>` | `/apps/<产品 ID>` |

用户可在 GUI / TUI 修改路径；工程可锁定路径及范围。系统安装需要先以管理员权限启动安装器，当前版本不会自动进行 UAC / pkexec 提权。

### 输出形式

| `--format` | 结果 |
| --- | --- |
| `installer`（默认） | 目标原生可执行文件附加清单与数据；Windows EXE / Linux RUN / XJ380 ELF |
| `epf` | XJ380 原生 EPF，使用官方 elf2epf，OS ABI `0x0380`、ABI 版本 1；单文件上限 256 MiB |
| `archive` | 独立 `.acpkg` 数据包，由模板通过 `--package` 安装 |
| `bundle` | 包含原生模板、`application.acpkg` 和运行说明的目录 |

支持 `--compression lz4|store`、`--output`、`--stub`。当前不生成 MSI / MSIX / DEB / RPM / AppImage；可基于原生模板继续添加输出后端。

## 安装、检查和卸载

```bash
# 明确选择交互方式
./setup.run --ui gui
./setup.run --ui tui

# 仅输出计划，不写入文件或注册信息
./setup.run --dry-run --dir "$HOME/Apps/toolbox"

# 自动化安装；有协议时必须显式接受
./setup.run --silent --accept-license --dir "$HOME/Apps/toolbox" \
  --deselect environment:path --select shortcut:desktop-cli

# 检查安装器或数据包清单
./build/apack inspect --package ./setup.run

# 运行已安装的其他应用入口；保留配置参数并追加参数
"$HOME/Apps/toolbox/.accretion/launch" --application cli -- file.txt

# 卸载器也提供 GUI / TUI 和静默模式
"$HOME/Apps/toolbox/.accretion/uninstall" --ui tui
"$HOME/Apps/toolbox/.accretion/uninstall" --silent
```

Windows 的辅助程序扩展名为 `.exe`，XJ380 为 `.elf`。安装收据位于 `<安装目录>/.accretion/receipt.json`，记录选项、文件校验值和集成前后的快照。升级时相同产品 ID 复用已有选择，清理旧文件并保留修改过的配置；卸载保留被用户修改或另行创建的文件。

安装失败或用户在 GUI 取消时尝试回滚。断电或进程被终止会留下相邻的 `<安装目录>.accretion-txn` 目录；确认没有安装进程运行后，用原安装器恢复：

```bash
./setup.run --recover --dir /absolute/install/directory
```

不要在仍有安装进程运行时恢复。恢复完成后可重新安装。Windows 卸载器会复制到缓存目录运行，再删除应用目录；缓存副本可能保留到后续清理。

## 环境变量与系统集成

环境变量逐项显示复选框，省略 `defaultChecked` 时默认勾选。支持 `set`、`setIfMissing`、`appendUnique`、`prependUnique`，以及 `inherit`、`application`、`user`、`machine` 范围。

Windows 持久写入对应用户或系统注册表，并发送设置变更通知。Linux 写入独立的 Shell 配置文件、受管理的 `.profile` 引用块（zsh 会额外处理 `.zprofile`）及 environment.d；新会话加载环境，现有终端不被更改。Shell 配置与启动器支持去重；environment.d 使用 systemd 的变量扩展规则，空值及重复路径的语义取决于会话管理器。通过安装器生成的快捷方式或启动器启动时，也会即时注入已选择的变量。

Windows 注册程序卸载信息、可选的自定义注册表项和文件打开方式。Linux 注册 `.desktop` 与应用专有的 MIME 扩展名，提交后调用系统缓存更新工具；缺少工具时报告警告。文件关联登记为可用应用，默认打开程序由系统和用户选择。

XJ380 的 `inherit` / `application` 环境变量保存在应用收据中，由原生启动器传给 `xapi_Execve`；默认快捷方式是可执行 ELF。**全局环境变量、系统快捷方式格式、文件关联及指定启动工作目录使用预留的原生适配器，待系统接口补齐后接入。** 没有相应能力时，显式请求这些功能会在安装前报错。[适配器约定](docs/xj380-adapter.md)与[可编译骨架](examples/xj380_adapter.cpp)已提供。

## 验证与当前边界

```bash
./build/ap-core-tests
./build/ap-xapi-tests
./build/ap-xj-adapter-tests
python3 tests/integration.py --build-dir build
# WSL 可执行 Windows 程序时追加原生 Windows 测试
./build/ap-windows-registry-tests.exe
python3 tests/integration.py --build-dir build --windows

# AddressSanitizer / UndefinedBehaviorSanitizer（Linux）
python3 tools/build.py --target linux --out build/sanitize --sanitize
./build/sanitize/ap-core-tests
python3 tests/integration.py --build-dir build/sanitize
```

已在当前 Linux / WSL + Windows 环境验证原生 GUI 初始化、TUI 交互、真实安装与卸载、中文与含空格路径、快捷方式、注册表读写、环境选项、升级回滚、跨文件系统恢复、包损坏与路径校验，以及 EPF 的 ABI / 分段 / 完整性。Qt 工作台已编译并通过无显示启动检查。

**XJ380 已完成原生 ELF 编译、XAPI ABI 与适配器契约测试、EPF 结构和载荷验证，尚未在 XJ380 系统实际运行。** 原生 GUI、权限授权、文件重命名替换和电源故障行为仍需目标系统验证。当前安装器使用 UTF-8 中文界面；协议可按语言键配置。输入只打包普通文件，内部文件链接可解引用，目录链接、外部链接和特殊文件会被拒绝。校验值用于检测损坏，不构成发行者签名；当前未验证 PE 签名工具追加证书后的尾部定位，不应直接假设签名后的安装器可用。

实现结构见 [docs/architecture.md](docs/architecture.md)，第三方来源与许可证见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
