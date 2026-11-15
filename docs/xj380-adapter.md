# XJ380 原生集成预留接口

根据当前 XAPI 1.4 文档，实现已直接绑定原生文件、内存、权限、进程、窗口、绘图和输入 API。全局环境变量、桌面快捷方式格式、文件关联及指定启动工作目录没有在该版本文档中定义，按项目要求保留为可接入接口。没有假设或占用未来系统调用编号。

声明在 [include/ap/xj380_adapter.h](../include/ap/xj380_adapter.h)，可编译骨架在 [examples/xj380_adapter.cpp](../examples/xj380_adapter.cpp)。OS API 完成后，提供一个强定义：

```cpp
extern "C" const ap_xj380_adapter_v1* ap_xj380_query_adapter_v1();
```

返回的结构体及字符串必须在整个进程生命周期有效。当前版本 `version=1`，x86-64 `bytes=72`；不支持的版本或过短结构会被忽略。实现文件应仅调用原生 XAPI，运行在安装器稳定的用户主线程，权限请求遵循 OS 原有授权流程。

```bash
# 轻量构建脚本
python3 tools/build.py --target xj380 --xj-adapter path/to/native_adapter.cpp

# 或 CMake，将适配器直接链接进两个原生模板
cmake -S . -B build/xj380 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/xj380-toolchain.cmake \
  -DAP_XJ380_ADAPTER_SOURCE=/absolute/path/native_adapter.cpp
cmake --build build/xj380
```

适配器源文件直接链接到可执行文件，确保强定义覆盖运行库的弱实现。构建模板后，仍由宿主 `apack` 生成 ELF / EPF 安装程序。对现有安装的后续系统集成升级，需要使用包含该适配器的新版本安装器重新安装。

## 能力位

| 常量 | 值 | 回调 | 无能力时 |
| --- | --- | --- | --- |
| `AP_XJ380_CAP_ENVIRONMENT` | 1 | `read_setting`、`write_setting` | `inherit` / `application` 变量随应用持久保存；显式 `user` / `machine` 在预检时报错 |
| `AP_XJ380_CAP_SHORTCUTS` | 2 | `write_shortcut`、`shortcut_extension` | 生成可执行 `.elf` 快捷启动器 |
| `AP_XJ380_CAP_ASSOCIATIONS` | 4 | `read_setting`、`write_setting` | 请求文件关联时报错 |
| `AP_XJ380_CAP_LAUNCH` | 8 | `launch` | 使用原生 Fork / Execve；显式工作目录时报错 |

若能力位被设置但必需回调为空，对应能力会被屏蔽。快捷方式扩展名必须以点开头，后续使用 ASCII 字母、数字、下划线或连字符，总长度不超过 19 字符。可分批实现，只设置已完成的能力。

`desktop_directory(machine)` 可选，返回当前用户或系统桌面目录；未提供时，当前用户目录使用 `/users/<用户名>/desktop`（包括未提供机器级目录时）。`changed()` 可选，在安装 / 卸载提交后通知系统环境、桌面和关联数据已变更。

## 设置快照

`read_setting(descriptor, buffer, capacity, needed)` 采用两次调用协议：

1. `buffer=nullptr, capacity=0` 查询 UTF-8 JSON 缓冲区大小。
2. `needed` 包含结尾 NUL。成功返回 `0`；缓冲区不足返回 `-28` 并写入所需大小；其他负值表示错误。
3. 实际读取时保证输出 NUL 结尾、更新 `needed`。安装器限制快照最多 1 MiB，大小变化最多重试三次。

`write_setting(descriptor, snapshot)` 应恢复或设置完整快照，成功返回 `0`，失败返回负错误码。快照必须包含布尔 `exists`；`exists=false` 表示移除该设置。`value` 可使用原生结构所需的 JSON 值，必须能无损往返。

环境变量描述符：

```json
{"kind":"environment","scope":"user","name":"TOOLBOX_HOME"}
```

对应快照示例：

```json
{"exists":true,"value":"/users/demo/Applications/com.example.toolbox"}
```

`scope` 为 `user` / `machine`。环境变量快照的 `value` 为字符串。安装器在写入前读取快照，用于事务回滚；卸载恢复未被外部修改的旧值，追加 / 前置操作只移除安装时新增的元素。列表分隔符当前为 `:`。

文件关联描述符：

```json
{"kind":"association","scope":"user","id":"com.example.toolbox.document"}
```

关联快照的 `value` 包含 `id`、`application`、`description`、`extensions`，以及由安装器补充的绝对 `executable` 和参数数组 `arguments`。`executable` 是应用辅助启动器，参数通常为 `["--application","main","--","%1"]`；这里 `%1` 是适配器需要映射为原生“待打开文件”的占位符。用户设定的默认程序归 OS 管理。

## 快捷方式与启动

`write_shortcut(output, executable, arguments, working_directory, icon)` 必须在给定 `output` 写出**独立普通文件**；`arguments` 是 NUL 终止的 UTF-8 字符串指针数组。该路径为事务暂存路径，成功后安装器将文件复制到桌面 / 菜单再原子替换。因此快捷方式不能把自身暂存路径写入内容，不能依赖其文件名选择应用，也不应同时写额外旁文件。成功 `0`，失败负值。卸载由文件快照管理。

`launch(executable, arguments, environment, working_directory, visitor)` 的两个数组均以空指针终止，`environment` 元素为 `KEY=VALUE`。`arguments` 包含原生 argv[0]，`visitor` 非零要求启动时降为 Visitor，不能沿用安装器的高权限。返回非负值表示已成功创建进程，负值表示失败；必须在子进程启动错误时返回失败。不可仅创建进程后无条件报告成功。`working_directory` 为展开后的绝对路径。

安装器负责环境变量选择、占位符和入口参数组合；适配器负责 OS 格式、全局存储、原生进程目录及权限语义。回调应幂等，特别是 `exists=false` 对已经不存在的设置也应成功。回调失败会触发其他文件和设置的逆序回滚。

## 已有验证与待系统验证

`tests/xapi_abi.cpp` 检查 XAPI 原始调用编号、参数顺序、错误返回值与结构大小；`tests/xj_adapter.cpp` 检查版本过滤、能力屏蔽、快照缓冲区、解析和键顺序无关的所有权比较。原生 ELF 无动态解释器，EPF 采用 OS ABI `0x0380`。这些测试不等价于 XJ380 真机验证。

在目标系统需确认 GUI 消息 / 文本框行为、许可滚动、权限授权和降权、UTF-8 路径、文件替换与删除、`argv` / `envp` 约定，以及断电后的事务日志恢复。系统适配器补齐后，还需实际验证全局设置恢复、关联注销和桌面格式，随后才能宣称对应系统集成可用。
