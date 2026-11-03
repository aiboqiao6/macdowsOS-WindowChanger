# v8 本机兼容记录（2026-10-31）

本轮重点修复纯 Win32 白边、资源管理器直角，以及首次显示和撤销时的渲染残留。所有修改直接发生在目标进程及原窗口内，没有窗口跟随、覆盖窗口或额外标题行。默认请求管理员权限，后端按框架能力与原布局选择。

环境：Windows 11 build 26100，x64，150% DPI。使用原生 Win32/GDI 屏幕复制及必要的原生输入，没有使用 Computer Use。

## 本轮修复

原生 Win32 保留原 NCCALCSIZE、外框和客户区尺寸，分别绘制原非客户区四边，避免 DWM 透明缩放边距变成白条。缓冲绘制使用 BPPF_NOCLIP；区域裁切保留完整最后一行、最后一列，正常窗口按原可见轮廓圆角，最大化使用矩形。撤销在主题和 DWM 完成重新计算后恢复原区域。

原生菜单和滚动条继续使用原区域、命中测试和输入处理。深色滚动条在宿主绘制回调及相关输入处理后更新，修复控制台最大化还原后出现浅色轨道的问题。

嵌入式 Chromium/D3D 子窗口可能缓存旧 DWM 坐标，忽略同尺寸 WM_SIZE。根据标准渲染子窗口能力发现这类表面，在附加与撤销阶段执行一次 1 像素尺寸往返，确认前恢复精确尺寸。这是一次性的重排，不是定位定时器。CC Switch 的首次内容覆盖标题区、撤销黑带由此修复，没有使用其应用名、路径或业务控件名。

NetUI/Ribbon 保留 DWM 非客户区、透明缩放边距和系统圆角，不应用 Win32 的 HRGN 裁切。真实 WS_SYSMENU 隐藏原系统按钮，原顶部行插入 84 DIP spacer，让原 QAT/标题布局腾出空间；三灯在原 HWND 绘制。进程内只读 API 兼容层让原框架继续读到原 WS_SYSMENU 与 caption bounds，避免首次、最大化还原后丢失标题行。撤销先清除三灯 alpha，再移除 spacer。

同一进程内多个 NetUI 窗口共享兼容层引用计数，分别撤销；最后一个 lease 释放后恢复原 IAT 地址。已通过实际多窗口与原导入地址比较验证。

## 检查方法

保存应用前、确认后约 100ms、稳定显示、最大化、还原大小、撤销后的完整画面。逐 HWND 检查三灯像素、原按钮、外框与客户区、无新增子 HWND、四边、四角、DWM 状态、原区域恢复、撤销残留及按钮响应。四边像素按实际可见边界采样，不把透明边距当作白边；原生滚动条单独采样。人工检查完整窗口和四角放大图。

自有示例验证红灯关闭；实际用户窗口只验证绿灯最大化/还原、黄灯最小化和撤销，不关闭用户应用。窗口截图时临时前置及规范尺寸，结束后恢复原始位置、状态和前台。

逐窗口最终数据及图片见 [截图报告](out/live-v8/release-screenshots/report.md) 和 [原始指标](out/live-v8/release-screenshots/report.json)，按钮记录见 [输入检查](out/live-v8/input-final/results.json)，多窗口撤销见 [NetUI lease 检查](out/live-v8/netui-leases/results.json)。真实截图包括检测期间新出现的 QQ、VMware 和 CC Switch。

## 范围与限制

这是统一的注入、能力探测、后端和撤销框架，不能据此声称任意窗口全部兼容。

Windows Terminal 与任务管理器：现有公开 XAML 根发现未通过，未接管。Edge：缺少原生 Chromium Views 的通用布局后端。设置：当前 UWP/ApplicationFrame 原布局没有可用后端。以上保持原样，不计为适配通过。

NetUI 限于当前 UIRibbon.dll CodeView `FEE19CF7-23B1-A0AD-09AC-63E3F230B785` age 1 及已验证原树结构。读取兼容层只处理已加载模块的普通/已解析延迟导入；跳过其他组件已有 hook 和未解析 delay thunk。后续新模块加载和跨屏 DPI 尚未验证。

Qt 仅验证 6.11.1 x64 Widgets/Quick 示例；Electron 验证必要的 Node-API/V8 导出与 major 13/15 ABI。定制初始 overlay 配色的精确还原、页面刷新及软件升级仍需单独验证。管理员权限不能绕过受保护进程或安全桌面。

![四边和系统圆角实拍](images/window-frame-v8.png)

## 最终构建结果

实际窗口通过 11；验证失败 0；未适配 4；系统/后台/辅助窗口排除 4。自有 Explorer 研究窗口已关闭，未计入这些通过数。

运行时：`MacdowsStyleHook64-d2961fd591df.dll`。x64 CTest 12/12，x86 CTest 6/6；SDK 示例独立编译通过。11 个实际窗口的绿灯最大化/还原、黄灯最小化及撤销通过。

| HWND | 应用窗口 | 框架 | 结果 | 附加确认 ms |
| --- | --- | --- | --- | --- |
| 0x6104b4 | ChatGPT | Electron | captured | 321 |
| 0x20032 | CC Switch | Win32 | captured | 160 |
| 0x8f0c0e | D:\DEV\macdowsOS tool UI\demos\window-styler | NetUI / Ribbon | captured | 179 |
| 0x230802 | D:\DEV\MC-Service | NetUI / Ribbon | captured | 201 |
| 0x2b0f2e | 通用 x86 调优工具 | WPF | captured | 213 |
| 0x4f07ae | macdowsOS tool UI - mydockfinder_Install.h - Microsoft Visual Studio(管理员) | WPF | captured | 205 |
| 0x3d156c | 控制面板\所有控制面板项 | Win32 Shell | captured | 156 |
| 0xf00d28 | MC-Service 1.20.1 / Java + Bedrock Console | Console host | captured | 412 |
| 0xbb09da | VMware Workstation | Win32 | captured | 160 |
| 0x309f2 | QQ | Electron | captured | 179 |
| 0x1e17e0 | C:\Users\aiboqiao6\Desktop\ChmlFrp-0.51.2_251023_2_windows_amd64(1) | NetUI / Ribbon | captured | 188 |
| 0xc009cc | 设置 | UWP / Windows XAML | unsupported |  |
| 0x3213a4 | 任务管理器 | WinUI / XAML Islands | unsupported-layout | 894 |
| 0x2094a | Windows PowerShell | WinUI / XAML Islands | unsupported-layout | 894 |
| 0x59047c | aiboqiao6/macdowsOS-Widget: 仿 macOS 的第三方桌面小组件 - 个人 - Microsoft​ Edge Beta | Chromium Views | unsupported-layout | 200 |

分发包管理员控制器 14/14 检查通过：自动接管、x64/x86 helper、Qt/WPF 原布局、暂停/恢复、CLI lease 到期及控制器异常退出后的撤销。见 `out/controller-direct-v8/checks.json`。

完整画面保存在工作区；分发包的 `evidence` 目录附逐窗口指标、输入记录、多窗口撤销和构建回归记录，`images` 附标题与四角对比。

管理员 v8 控制器已实际启动；11 个已验证窗口全部确认接管，检查后还原了原位置及最小化/最大化状态。证据：`out/live-v8/startup.json`、`running-inventory.json`。
