# 一键构建与发布目录

在仓库根目录运行：

```bash
make build             # 配置并增量编译当前平台
make package           # 先构建，再生成完整发布目录
make test              # 构建并运行原生 CTest
make check             # 检查构建工具与配置路径
```

只需要一次安装构建工具：Python 3.10+、CMake 3.20+ 和对应 C++17 工具链。图形工作台还需要 Qt 6 Widgets 开发包。没有 Qt 时默认构建终端前端（`apack wizard`）；显式 `AP_FRONTEND=gui` 则要求 Qt，缺失会报错，不会悄悄省略图形前端。

Windows 不需要安装 make。在命令提示符 / PowerShell 中使用：

```powershell
.\build.cmd
.\package.cmd
# 或所有系统通用的入口
python tools/project.py build
python tools/project.py package
```

Linux / WSL 对应 `python3 tools/project.py build|package|test|check`。Make 的 Python 可通过 `PYTHON=python` 覆盖。

## 版本、前后端与目标平台

前端是 Qt 工作台或终端向导，后端是本地 `apack`；前端直接调用同目录下的后端。因此一份发布目录中的前后端必须面向同一宿主平台。安装器模板是另一组目标，可选择 Windows、Linux、XJ380，仍包含原生 GUI / TUI。

如果只选择一种安装模板 UI，打包应用时也要使用对应的 `--ui gui` 或 `--ui tui`；项目默认 UI 和工作台里的选择不会因删除另一种模板而自动改写。

```bash
# Linux 图形工作台 + 后端 + 三平台全部 GUI/TUI 安装模板
AP_PLATFORM=linux AP_FRONTEND=gui AP_RUNTIME_PLATFORMS=all make package

# 仅终端前端与后端，并提供原生 XJ380 GUI/TUI 模板
AP_FRONTEND=tui AP_RUNTIME_PLATFORMS=xj380 make package

# 使用自定义 Qt SDK，只分发 GUI 安装模板
AP_QT_PREFIX=/opt/Qt/6.x/gcc_64 AP_FRONTEND=gui AP_RUNTIME_UI=gui make package

# 相同选项也有命令行形式，显式参数优先于环境变量
python3 tools/project.py package --platform linux --frontend gui --runtime-platforms all
```

PowerShell 使用 `$env:` 设置环境变量：

```powershell
$env:AP_PLATFORM = 'windows'
$env:AP_FRONTEND = 'gui'
$env:AP_QT_PREFIX = 'C:/Qt/6.x/msvc2022_64'
.\package.cmd
```

| 环境变量 | 默认值 | 用途 |
| --- | --- | --- |
| `AP_PLATFORM` | 当前系统 | 打包工具的宿主平台：`windows` / `linux`（也接受 `*-x64`） |
| `AP_FRONTEND_PLATFORM` | 后端平台 | 显式指定前端平台，须与同目录后端一致 |
| `AP_BACKEND_PLATFORM` | `AP_PLATFORM` | 显式指定 `apack` 平台 |
| `AP_FRONTEND` | `auto` | `auto` 有 Qt 则 GUI，否则 TUI；`gui` 强制 GUI；`tui` 关闭 Qt 构建 |
| `AP_RUNTIME_PLATFORMS` | 后端平台 | 安装器模板平台：`all` 或逗号分隔的 `windows,linux,xj380` |
| `AP_RUNTIME_UI` | `both` | 模板默认前端：`gui` / `tui` / `both` |
| `AP_BUILD_TYPE` | `Release` | `Release` / `Debug` / `MinSizeRel` / `RelWithDebInfo` |
| `AP_JOBS` | 最多 4 | 并行编译数 |
| `AP_QT_PREFIX` | CMake 自动发现 | Qt 6 SDK 前缀，与前端平台及编译器匹配 |
| `AP_CMAKE` | `cmake` | CMake 可执行文件路径 |
| `AP_TOOLCHAIN_FILE` | 原生，或 Windows MinGW 交叉工具链 | 自定义 CMake 工具链 |
| `AP_XJ380_ADAPTER_SOURCE` | 无 | 已实现系统接口的 XJ380 原生适配器源码 |
| `AP_STUB_DIR` | 自动交叉构建 | 已编译的额外目标模板目录，可用于不具备该交叉工具链的宿主 |
| `AP_EXTRA_LIB_DIRS` | 无 | 收集运行依赖时的额外查找目录；使用系统路径分隔符 |
| `AP_BUILD_DIR` | `build/oneclick/<平台>-<构建类型>` | 独立的增量构建目录 |
| `AP_PACKAGE_DIR` | `dist/accretion-package-<平台>-x64` | 最终发布目录 |

当前宿主打包器支持 Windows / Linux；XJ380 原生安装器使用 `AP_RUNTIME_PLATFORMS=xj380` 选择。Linux / WSL 交叉构建 Windows 模板需要 Clang、MinGW-w64 C 工具链与头文件，XJ380 模板需要 Clang 和 GNU ELF 链接器。交叉构建整个 Windows 打包工具还需要 MinGW C++ 工具链及匹配的 Windows Qt SDK；也可在 Windows 上原生构建。跨宿主平台不能混用 Qt 库。

如果 Windows 宿主没有 XJ380 ELF 链接工具，可在 Linux / WSL 生成 XJ380 模板后，通过 `AP_STUB_DIR` 提供。原先的 `tools/build.py --target ...` 轻量构建入口保留，可单独生成模板。

## 发布目录

```text
dist/accretion-package-linux-x64/
  apack                         # 后端启动入口，保留调用者工作目录
  start-frontend.sh             # GUI 或 TUI 前端入口
  check-deps.sh                 # 无 Python 的运行依赖检查
  START_HERE.txt
  package-manifest.json         # 平台、前端、模板与文件 SHA-256
  bin/
    apack
    apack-workbench             # GUI 版才包含
    qt.conf                    # GUI 版的相对插件路径
    ap-stubs/                  # 所选目标与 UI 的原生模板
  lib/                         # 收集的可分发动态库
  plugins/                     # Qt 平台、图像等插件
  licenses/
  share/docs/
  share/schema/
```

Windows 对应根目录入口为 `apack.cmd`、`start-frontend.cmd`、`check-deps.cmd`，并包含 `check-deps.ps1`；实际 EXE/DLL 位于 `bin/`。可以直接调用 `bin/apack.exe`。Qt GUI 通过同目录的后端工作，不需要额外后台服务。

发布包只依赖这个目录和基础系统运行环境：不需要原仓库、build 目录、Python、CMake、编译器或 Qt SDK。移动整个目录、放到含空格或中文路径都可使用；启动脚本不依赖当前工作目录。GUI 版需保留 `bin/qt.conf` 和插件目录。

```bash
cd /any/working/directory
/path/to/accretion-package-linux-x64/check-deps.sh
/path/to/accretion-package-linux-x64/start-frontend.sh
/path/to/accretion-package-linux-x64/apack --help
# 也可直接调用有相对运行库路径的二进制
/path/to/accretion-package-linux-x64/bin/apack --help
```

`make package` 自动先构建，再用临时目录收集和校验文件，成功后替换旧发布目录。失败不会破坏上一份发布目录；不会覆盖没有 `package-manifest.json` 标记的其他文件夹。

## 运行依赖检查

Linux 使用系统 `sh` 与 `ldd`，Windows 使用系统 PowerShell 和 PE 导入表读取，无须安装 Python 或开发工具：

```bash
./check-deps.sh                # 检查后端、前端及当前桌面平台插件
./check-deps.sh --backend      # 无显示服务器时只检查后端
QT_QPA_PLATFORM=offscreen ./check-deps.sh --frontend
```

```powershell
.\check-deps.cmd
.\check-deps.cmd -Mode backend
```

缺少库、Qt 插件、配置文件或不兼容的 GLIBC 版本时，脚本列出名称并返回非零退出码。Linux 的 glibc / 动态加载器由目标系统提供，不能假设新系统构建的二进制能在更旧的 glibc 上运行；发行时应在计划支持的最旧系统上构建。显示服务、显卡驱动也由系统提供。执行 Linux GUI 安装器时还需要目标系统 GTK 3，这与 Qt 打包工作台的依赖分开。

Windows GUI 版使用匹配 SDK 的 `windeployqt` 收集 Qt 文件；如需 Microsoft Visual C++ 运行环境，依赖检查会提示，并列出工具所附带的官方运行环境安装程序。原生 MSVC 后端与安装模板使用静态 CRT。不会自动修改系统环境或安装系统运行库。

可用 `python3 tests/packaging.py --package-dir dist/accretion-package-linux-x64` 验证发布目录：复制到新目录、清空 SDK 环境变量，从另一个工作目录调用前后端并实际构建安装包。GUI 启动检查使用 Qt 自带 offscreen 平台。

部署约定参考：[Qt 共享库与插件](https://doc.qt.io/qt-6/linux-deployment.html)、[qt.conf 相对路径](https://doc.qt.io/qt-6/qt-conf.html)、[Windows windeployqt](https://doc.qt.io/qt-6/windows-deployment.html)。
