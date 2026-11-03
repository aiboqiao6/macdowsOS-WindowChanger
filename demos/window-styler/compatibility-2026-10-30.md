# 当前电脑逐窗口兼容性报告

2026-10-30；Windows 11 build 26100；144 DPI / 150% 缩放；本次真实应用均为 x64；Hook 源码摘要 `29a28d1cb801`。

共盘点 **17 个窗口**：**10 个应用窗口通过，0 个应用窗口检查失败，1 个传统控制台窗口未支持，6 个系统/隐藏/辅助窗口排除**。普通权限检查 7 个通过；经用户同意 UAC 后，管理员检查 3 个通过。每个通过窗口执行 26 项断言，共 260 项。

通过范围：真实跨进程附加；首次应用后及 1.2 秒后的标题栏绘制调用、标题拖动区和红黄绿按钮命中；客户区尺寸；左右及下边距；首次完整刷新；菜单和样式位；还原确认、DWM 状态、圆角、原始位置、正常尺寸、最小化/最大化状态和响应性。真实应用没有执行关闭、输入、内容修改或最大化操作；最小化的受支持窗口临时显示后还原。

本次全部采用命令行和 Win32 API，未使用 Computer Use 或采集截图。GDI / WM_PAINT / 刷新计数和几何检查不等于 GPU 最终画面的逐像素认证；最终视觉效果仍需人工确认。扩展标题栏保留应用客户区内原有标签页、工具栏和自绘窗口按钮。

## 每个窗口的结果

| 窗口 | HWND | 窗口类型 | 结果 | 实测数据 / 原因 |
| --- | --- | --- | --- | --- |
| 开始 | 0xa0cae | Button | 排除 | 桌面、任务栏及其按钮不属于应用窗口 |
| D:\DEV\macdowsOS tool UI\demos\window-styler | 0x8f0c0e | CabinetWClass | 通过 26/26 | 标题栏 54 px；左/右/下边距 2/2/2 px；客户区 2142×1489 |
| 任务管理器 | 0x850eaa | TaskManagerWindow | 通过 26/26 | 标题栏 54 px；左/右/下边距 2/2/2 px；客户区 2328×1163 |
| ChatGPT | 0x6b0612 | Chrome_WidgetWin_1 | 通过 26/26 | 标题栏 54 px；左/右/下边距 2/2/2 px；客户区 2525×1506 |
| D:\DEV\macdowsOS tool UI\demos\window-styler\out\build-x64\Release | 0x230802 | CabinetWClass | 通过 26/26 | 标题栏 54 px；左/右/下边距 2/2/2 px；客户区 2142×1489 |
| QQ | 0x309f2 | Chrome_WidgetWin_1 | 通过 26/26 | 标题栏 54 px；左/右/下边距 2/2/2 px；客户区 2462×1688 |
| macdowsOS tool UI - mydockfinder_Install.h - Microsoft Visual Studio(管理员) | 0x4f07ae | WPF / HwndWrapper | 通过 26/26 | 标题栏 54 px；左/右/下边距 2/2/2 px；客户区 2912×1853 |
| aiboqiao6/macdowsOS-Widget: 仿 macOS 的第三方桌面小组件 - 个人 - Microsoft​ Edge Beta | 0x59047c | Chrome_WidgetWin_1 | 通过 26/26 | 标题栏 54 px；左/右/下边距 2/2/2 px；客户区 2750×1606 |
| 通用 x86 调优工具 | 0x2b0f2e | WPF / HwndWrapper | 通过 26/26 | 标题栏 54 px；左/右/下边距 2/2/2 px；客户区 1620×960 |
| 设置 | 0x7f01e4 | Windows.UI.Core.CoreWindow | 排除 | DWM 隐藏窗口（其他桌面或系统后台），当前没有可见画面可检查 |
| 设置 | 0x4603ec | ApplicationFrameWindow | 排除 | DWM 隐藏窗口（其他桌面或系统后台），当前没有可见画面可检查 |
| MC-Service 1.20.1 \| Java + Bedrock Console | 0x3108d4 | ConsoleWindowClass | 未支持 | 传统控制台宿主不支持当前进程内附加路径；没有应用样式 |
| Windows PowerShell | 0x2094a | CASCADIA_HOSTING_WINDOW_CLASS | 通过 26/26 | 标题栏 54 px；左/右/下边距 2/2/2 px；客户区 1730×925 |
| C:\Windows\System32\cmd.exe - frpc | 0x380574 | CASCADIA_HOSTING_WINDOW_CLASS | 通过 26/26 | 标题栏 54 px；左/右/下边距 2/2/2 px；客户区 1730×925 |
| com.ccswitch.desktop-siw | 0x200e4 | com.ccswitch.desktop-sic | 排除 | 工具 / 托盘辅助窗口，不作为主应用窗口替换标题栏 |
| Windows 输入体验 | 0xa0204 | Windows.UI.Core.CoreWindow | 排除 | DWM 隐藏窗口（其他桌面或系统后台），当前没有可见画面可检查 |
| Program Manager | 0xa0c8a | Progman | 排除 | 桌面、任务栏及其按钮不属于应用窗口 |

## 检查中发现并修正的问题

- 资源管理器、任务管理器虽然带 WS_CAPTION，实际顶部由客户端绘制。改为同时检查真实客户区起点，选择扩展标题栏，避免仅按类名或样式位误判。
- 原先把所有 WS_EX_TOOLWINDOW 都视为辅助窗口，会误排除调优工具。现在保留有标题/系统菜单的正常应用窗口，排除纯辅助窗口。
- 最小化的任务管理器会忽略无激活显示并主动抢占前台。检查工具在目标夺取焦点时恢复原前台；最终管理员复核通过。用户自行切换到其他应用时不强行切回。
- 原有首次应用刷新方案保留：真实非客户区标题带、细边框、客户区尺寸补偿、同步 WM_SIZE + RedrawWindow 和延后刷新。最终 10 个应用窗口无需拖动即可完成两次观测，客户区尺寸一致。
- Hook DLL 改为带源码摘要的文件名，避免已还原的目标进程仍驻留旧模块时误加载旧实现。模块本身保持驻留到目标进程退出。

## 回归与剩余限制

最终 x64 CTest 4/4 通过（195 项外层断言）；x86 CTest 2/2 通过（156 项外层断言）。包含五类窗口模型、逐窗口检查/还原、Qt 6.11.1 Widgets/Quick GPU 和 .NET 9 WPF 实际框架测试。逐窗口子进程内部断言另记在其报告中。

x86 回归过程中出现过一次最大化/还原超时并引发后续断言失败；未能稳定复现。独立重跑、连续三轮复跑以及最终完整 CTest 均通过，根因尚未确认，保留为待跟踪的间歇问题。本机真实桌面窗口此次均为 x64。

传统 ConsoleWindowClass、UWP 宿主和分层透明主窗口还没有通用适配器。DWM 隐藏的设置/输入窗口没有可见画面可实测，因此不标为通过。跨显示器 DPI、其他 Windows build、应用重建 HWND、辅助功能和 Snap 悬停尚需扩大验证。

## 原始证据

- [普通权限逐项报告](out/desktop-audit-20261030-final/report.md) / [JSON](out/desktop-audit-20261030-final/report.json)
- [管理员逐项报告](out/desktop-audit-20261030-elevated-final/report.md) / [JSON](out/desktop-audit-20261030-elevated-final/report.json)
- [合并后的逐窗口 JSON](out/desktop-audit-20261030-combined.json)
