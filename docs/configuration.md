# 工程字段与定制

工程使用 UTF-8 JSON，`schemaVersion` 当前为 `1`。`apack validate --project project.json` 执行同一套运行时字段检查；构建时另外验证目标入口、输入文件、图标和许可协议。未知字段报错，避免拼写错误被忽略。发布清单由构建器生成，不应手工填入 `target`、`runtime` 或编译后的文件索引。

## 应用和入口

`app` 包含稳定的 `id`、展示 `name`、`version`，可选 `publisher`、`description`。产品 ID 应在升级中保持不变，格式为字母或数字开头，其余允许字母、数字、点、下划线和连字符，最长 120 字符。ID 不是文件显示名称。

`inputs` 按 `windows-x64`、`linux-x64`、`xj380-x64` 指定输入目录。`applications` 是按入口 ID 索引的对象：

```json
{
  "applications": {
    "main": {
      "windows": "bin/editor.exe", "linux": "bin/editor", "xj380": "bin/editor.elf",
      "arguments": ["--data", "${install.dir}/data"],
      "workingDirectory": "${install.dir}", "terminal": false
    },
    "cli": { "windows": "bin/tool.exe", "linux": "bin/tool", "xj380": "bin/tool.elf", "terminal": true }
  },
  "completion": { "application": "main", "launchDefault": false }
}
```

可只声明工程所支持的系统；构建某个目标时，每个应用入口都必须有对应系统路径，且存在于载荷中。路径使用 `/`，相对于安装根目录。`arguments` 通过参数数组传递，不经过 Shell 拼接。`terminal` 决定 Linux 桌面项的终端属性，Windows 则由应用自身的 PE 子系统决定。XJ380 自定义 `workingDirectory` 需启用原生启动适配器；省略该字段时使用 OS 的原生启动目录约定。

## 安装与界面

`install.scope` 为 `user` 或 `machine`；`allowScopeChange`、`allowPathChange` 默认 `true`。`install.paths.<windows|linux|xj380>.<user|machine>` 可覆盖默认路径。安装目录必须为应用专用的绝对路径，不能使用根目录、路径穿越或符号链接 / reparse point。

`ui.default` 为 `gui` 或 `tui`；`ui.modes` 可设为 `["gui","tui"]` 或仅允许其中一种。模板都包含两个前端，工程控制哪些模式可以使用。`--ui auto` 尝试 GUI，无法初始化时回落到 TUI；无人值守请使用 `--silent`。当前展示文案使用中文，`ui.language` 用于选择许可协议文本。

## 图标与许可协议

```json
{
  "branding": {
    "installerIcon": "art/setup.ico",
    "applicationIcon": "art/app.png",
    "licenses": { "zh-CN": "licenses/zh-CN.txt", "en": "licenses/en.txt" }
  }
}
```

资源路径以工程目录为基准，构建器将资源加入 `_ap_assets/`。Windows 安装器图标接受 ICO 或最多 256×256 的 PNG，写入 PE 资源；建议 Windows 应用图标和快捷方式使用 ICO，Linux 使用 PNG / SVG，XJ380 使用原生图标加载器支持的格式。GUI 窗口图标也使用品牌配置。特定快捷方式可引用载荷中的另一图标文件。

协议在构建时读取并嵌入清单，交互安装必须接受；静默安装要求 `--accept-license`。仅显示约定语言、`en` 或第一个可用协议，不会把多个协议合并为多页独立条款。可通过单个协议文件组合条款。

## 环境变量

```json
{
  "environment": [
    { "id": "root", "name": "MYAPP_HOME", "value": "${install.dir}", "operation": "setIfMissing", "scope": "inherit", "defaultChecked": true },
    { "id": "tools", "name": "PATH", "value": "${install.dir}/bin", "operation": "appendUnique", "defaultChecked": true },
    { "id": "private", "name": "MYAPP_MODE", "value": "desktop", "scope": "application", "defaultChecked": false }
  ]
}
```

每条配置有独立复选框，省略 `defaultChecked` 时默认为 `true`。同一工程不允许重复变量名称；多个目录可通过应用自己的配置或单个值表达。

| 字段 | 取值与行为 |
| --- | --- |
| `operation` | `set` 覆盖；`setIfMissing`（默认）保留已有变量；`appendUnique` / `prependUnique` 增加一个列表元素 |
| `scope` | `inherit`（默认）随安装范围；`application` 仅传给通过辅助启动器启动的进程；`user` / `machine` 显式持久化 |
| `component` | 可选组件 ID；未安装该组件时跳过 |

Windows 列表分隔符为 `;`，Linux / XJ380 为 `:`。`appendUnique` 的值应是一个列表元素。XJ380 未接入系统适配器时，`inherit` 按应用级变量处理；显式全局范围会报错。Linux 的 Shell 配置与 systemd 用户环境加载阶段不同，现有进程不会被改变。全局变更生效通常需要新开会话，应用辅助启动器可立即注入变量。

卸载时仅恢复仍由该安装拥有的值；追加项会只移除安装时新增的元素。若用户已改写集成内容则保留并报告。多个独立产品共同使用同一 PATH 元素时，当前实现没有跨产品引用计数；应避免让不同产品共同拥有一个需要互相依赖的列表项。

## 快捷方式

```json
{
  "shortcuts": [
    { "id": "editor", "name": "我的编辑器", "application": "main", "location": "desktop", "defaultChecked": true },
    { "id": "cli", "name": "命令行工具", "application": "cli", "location": "menu", "arguments": ["--interactive"], "icon": "icons/cli.ico", "terminal": true, "defaultChecked": false }
  ]
}
```

`application` 引用入口 ID；`arguments` 追加到该入口配置参数之后。`icon` 为输入载荷中的相对路径；品牌图标作为默认值。名称中不适合作为文件名的内容会回退为产品与选项 ID。Windows `menu` 为程序菜单，Linux 为 XDG applications 目录，XJ380 为应用启动目录。全系统安装的桌面位置受各平台约定约束；Linux 机器级桌面项安装到共享 applications 目录。

## 可选组件与文件策略

```json
{
  "components": [
    { "id": "core-data", "name": "基础数据", "required": true, "inputs": { "linux-x64": "data/base" }, "destination": "data" },
    { "id": "extras", "name": "附加工具", "defaultChecked": false, "requires": ["core-data"], "inputs": { "linux-x64": "data/extras" }, "destination": "extras" }
  ],
  "files": { "include": ["*"], "exclude": ["*.pdb", ".git/*"], "preserve": ["config/*"], "dereferenceInternalLinks": true }
}
```

组件目录与主目录合并，不允许相同目标文件重复。`required` 组件不可取消；安装前检查依赖，未满足时要求重新选择。可将快捷方式或环境变量绑定到组件。

当前匹配规则是简单通配符：`*` 匹配任意数量字符（**包含 `/`**），`?` 匹配一个字符；`**` 与 `*` 等效。`include` / `exclude` 按各输入目录相对路径匹配，`preserve` 按最终目标路径匹配。目录项本身不保留，因此空目录不会单独打包。只支持普通文件；内部文件符号链接解引用后存为文件。特殊文件、目录链接、指向输入目录外的链接均报错。

`preserve` 用于配置文件：存在的用户文件保持原内容；升级与卸载也保留修改过的文件。未被修改的默认配置仍可在卸载时删除。卸载始终保留用户新增的非安装文件。

## 注册表与文件关联

```json
{
  "registry": [
    { "id": "path", "key": "Software/Example/Toolbox", "name": "InstallDir", "value": "${install.dir}", "type": "string", "scope": "inherit" },
    { "id": "version", "key": "Software/Example/Toolbox", "name": "ApiVersion", "value": 1, "type": "dword" }
  ],
  "associations": [
    { "id": "document", "application": "main", "description": "工具箱文档", "extensions": [".toolbox"] }
  ]
}
```

注册表支持 `string`、`expandString`、`dword`。范围决定 HKCU / HKLM，不接受任意 hive；键使用 `/` 或 `\`，采用 64 位视图，事务记录原始类型与值。注册表仅在 Windows 执行，其他系统报告忽略提示。

Windows 文件关联注册 ProgID、OpenWithProgids 和 RegisteredApplications / Capabilities。Linux 为每个关联生成 `application/x-<产品ID>.<关联ID>` MIME 定义及专用桌面项，并刷新缓存。这里适用于应用私有扩展名；对已有通用 MIME 类型的映射应在后续后端扩展中显式建模。XJ380 交给预留的 `association` 适配器。当前不提供“强制设为系统默认”的选项。

## 可用占位符

`${install.dir}`、`${app.id}`、`${app.name}`、`${app.version}`、`${user.home}`、`${paths.userPrograms}`、`${paths.machinePrograms}`、`${paths.desktop}`、`${platform}`、`${arch}`（当前为 `x64`）。环境变量值与安装路径中遗留的未知占位符会在安装前报错。字符串按字面值传递，不执行脚本。
