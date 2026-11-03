# v2 当前电脑兼容性报告

2026-10-30；Windows 11 build 26100；144 DPI / 150% 缩放；检查器及控制器均验证为高完整性。全程使用命令行和 Win32 API，没有使用 Computer Use，也没有采集截图。

新版默认请求管理员权限，自动管理同一用户下普通及管理员应用。保留现有标题栏、按钮、标签页、工具栏、尺寸及客户区；默认路径不注入 DLL，不添加第二个标题栏，也不显示最小化窗口。这个版本是减少干预的外观管理基础，还没有实现所有应用统一的 macOS 按钮和配色。

## 结果与适用范围

逐一记录 **17 个 HWND：11 个应用通过，0 个失败，6 个系统/隐藏/辅助窗口排除**。每个通过窗口执行 23 项断言，共 **253 项**。传统 ConsoleWindowClass 本次通过，解决了旧注入方案不能覆盖该宿主类型的限制。

本机全部 11 个应用只能安全回读圆角属性；边框、标题背景及文字颜色均不能回读，按策略保留原值。9 个应用将圆角偏好从默认值 0 调为 2；ChatGPT、调优工具原本为 2，没有新增外观改动。系统接受圆角偏好不等于保证所有窗口都呈现圆角，最大化、透明或自绘窗口仍由 DWM 和应用决定实际合成效果。

4 个应用检查时未最小化，7 个保持最小化。最小化窗口通过的是属性、几何和还原检查，未对恢复后的 GPU 最终画面作视觉认证。首次应用和 1.2 秒后都核对外框、客户区及起点、正常位置、状态、样式位、菜单和非客户区策略；这些值保持不变，因此这条路径没有引入布局变化或依赖拖动的补偿流程。

## 逐窗口记录

| 窗口 | HWND | 类型 / 完整性 | 结果 | 本次实际改动 |
| --- | --- | --- | --- | --- |
| 开始 | 0xa0cae | Button / medium | 排除 | 桌面、任务栏及其按钮不属于应用窗口 |
| ChatGPT | 0x6b0612 | Chrome_WidgetWin_1 / medium | 通过 23/23 | 原本为圆角偏好 2；原样保留 |
| MC-Service 1.20.1 \| Java + Bedrock Console | 0x32160e | ConsoleWindowClass / high | 通过 23/23 | 仅圆角偏好 0 → 2；检查后恢复 0 |
| D:\DEV\MC-Service | 0x230802 | CabinetWClass / medium | 通过 23/23 | 仅圆角偏好 0 → 2；检查后恢复 0 |
| D:\DEV\macdowsOS tool UI\demos\window-styler | 0x8f0c0e | CabinetWClass / medium | 通过 23/23 | 仅圆角偏好 0 → 2；检查后恢复 0 |
| macdowsOS tool UI - mydockfinder_Install.h - Microsoft Visual Studio(管理员) | 0x4f07ae | WPF / HwndWrapper / high | 通过 23/23 | 仅圆角偏好 0 → 2；检查后恢复 0 |
| 任务管理器 | 0x850eaa | TaskManagerWindow / high | 通过 23/23 | 仅圆角偏好 0 → 2；检查后恢复 0 |
| QQ | 0x309f2 | Chrome_WidgetWin_1 / medium | 通过 23/23 | 仅圆角偏好 0 → 2；检查后恢复 0 |
| aiboqiao6/macdowsOS-Widget: 仿 macOS 的第三方桌面小组件 - 个人 - Microsoft​ Edge Beta | 0x59047c | Chrome_WidgetWin_1 / medium | 通过 23/23 | 仅圆角偏好 0 → 2；检查后恢复 0 |
| 通用 x86 调优工具 | 0x2b0f2e | WPF / HwndWrapper / high | 通过 23/23 | 原本为圆角偏好 2；原样保留 |
| 设置 | 0x7f01e4 | Windows.UI.Core.CoreWindow / medium | 排除 | DWM 隐藏窗口（其他桌面或系统后台），当前没有可见画面可检查 |
| 设置 | 0x4603ec | ApplicationFrameWindow / medium | 排除 | DWM 隐藏窗口（其他桌面或系统后台），当前没有可见画面可检查 |
| C:\Windows\System32\cmd.exe - frpc | 0x380574 | CASCADIA_HOSTING_WINDOW_CLASS / medium | 通过 23/23 | 仅圆角偏好 0 → 2；检查后恢复 0 |
| Windows PowerShell | 0x2094a | CASCADIA_HOSTING_WINDOW_CLASS / medium | 通过 23/23 | 仅圆角偏好 0 → 2；检查后恢复 0 |
| com.ccswitch.desktop-siw | 0x200e4 | com.ccswitch.desktop-sic / medium | 排除 | 工具 / 托盘辅助窗口，不作为主应用窗口替换标题栏 |
| Windows 输入体验 | 0xa0204 | Windows.UI.Core.CoreWindow / medium | 排除 | DWM 隐藏窗口（其他桌面或系统后台），当前没有可见画面可检查 |
| Program Manager | 0xa0c8a | Progman / medium | 排除 | 桌面、任务栏及其按钮不属于应用窗口 |

## 控制器和框架验证

- 默认入口嵌入 `requireAdministrator` 清单，运行时也确认高完整性；一个控制器同时接管普通和管理员应用。
- 真实界面流程 **74/74 项通过**：默认自动接管全部可接管窗口、新窗口加入、暂停、恢复、选中还原后不立即再次接管、暂停并全部还原、五类新窗口模型加入、正常退出后原始快照完整还原。
- x64 CTest **7/7** 通过：旧注入回归、新逐窗口审计、默认原位路径、真实 Qt 6.11.1 Widgets/Quick 和 .NET 9 WPF。默认路径覆盖普通、最小化、最大化窗口，不增加 Hook 模块；故障测试强制结束测试控制器后，独立守护进程还原原值，目标应用继续运行。
- x86 CTest **3/3** 通过：旧注入回归、新逐窗口审计及默认原位路径。
- x64 → x86、x86 → x64 的原位测试分别通过；跨架构不需要注入。x86 Toolhelp 无法列出 x64 模块时，该方向的模块枚举断言明确跳过，不伪记为通过。

旧版注入留下的 Hook DLL 可能仍驻留在此前测试过的应用中；本次验证的是模块数量没有增加。旧模块随应用进程退出释放，新默认路径不加载它们。这里没有为了清理历史模块而结束用户应用。

## 本轮修正

1. 管理员入口改为默认行为，用户/完整性判断改为同一用户且控制器权限不低于目标；一个管理员会话覆盖本机全部可接管应用。
2. 默认移除进程内标题栏附加，不再变更非客户区计算、窗口外框、客户区、样式位、菜单或 DWM 非客户区策略；不发送 WM_SIZE 或模拟拖动。
3. 逐窗口探测可回读属性，先保存全部可用原值，再仅修改必要项。无法回读的颜色不写入；原本已满足目标的值不重复写入。
4. 独立守护进程持有原始值；控制器退出后还原，校验 HWND/PID/线程身份，避免对复用 HWND 应用旧状态。
5. 自动接管等待窗口初始化稳定；选中还原有手动排除，全部还原会暂停自动接管，避免立即改回去。状态区分实际修改与原样保留。
6. 新审计不再为了检查而显示最小化应用，避免任务管理器等应用主动激活和首次布局重算。

## 尚未验证或未实现

当前报告只覆盖这台电脑当前窗口、本次 Windows build 和缩放比例；不承诺其他版本、跨显示器 DPI、所有透明窗口、安全桌面、其他用户或受保护进程。DWM 隐藏的设置窗口和输入体验没有可见画面，因此没有标为兼容通过。没有进行 GPU 最终像素检查。

所有应用统一替换为 macOS 红黄绿按钮尚未实现。Chromium、Qt、WPF 等自绘标题控件需要各自保持原布局的适配；仅靠公开 DWM 属性无法实现完整替换。本机实际生效范围就是圆角偏好。

守护进程可处理控制器异常结束；若控制器和守护进程都被强制结束，内存中的原值无法继续恢复。还原遇到系统持续拒绝时也不能保证成功，当前退出流程最多重试约 5 秒。

## 原始证据

- [管理员全窗口逐项报告](out/desktop-audit-20261030-v2/report.md) / [JSON](out/desktop-audit-20261030-v2/report.json)
- [检查前清单](out/desktop-audit-20261030-v2/inventory-before.json) / [检查后清单](out/desktop-audit-20261030-v2/inventory-after.json)
- [控制器 74 项检查](out/controller-audit-v2/result.json)，同目录含接管前、接管中、全部还原及退出后的快照。
- CTest 日志：`out/build-x64/Testing/Temporary/LastTest.log`、`out/build-Win32/Testing/Temporary/LastTest.log`。
- [旧版注入记录](compatibility-2026-10-30.md) 仅作为历史研究数据。
