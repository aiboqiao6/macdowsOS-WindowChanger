# v6 本机兼容性记录（2026-10-30）

**未完成全部适配。** 直接注入截图通过 7；失败 0；未适配 4；排除系统/后台窗口 6。

环境：Windows 11 build 26100，x64，150% DPI；管理员控制器。

构建：`MacdowsStyleHook64-81eb28b38246.dll`。

## 当前窗口

| HWND | 程序 | 框架 | 结果 |
| --- | --- | --- | --- |
| 0xa0cae | 开始 | Win32 | excluded |
| 0x6b0612 | ChatGPT | Electron | captured |
| 0x309f2 | QQ | Electron | captured |
| 0x4f07ae | macdowsOS tool UI - mydockfinder_Install.h - Microsoft Visual Studio(管理员) | WPF | captured |
| 0x2b0f2e | 通用 x86 调优工具 | WPF | captured |
| 0x3d156c | 控制面板\所有控制面板项 | Win32 Shell | captured |
| 0x2094a | Windows PowerShell | WinUI / XAML Islands | captured |
| 0x1e17e0 | C:\Users\aiboqiao6\Desktop\ChmlFrp-0.51.2_251023_2_windows_amd64(1) | Win32 Ribbon / DirectUI | unsupported |
| 0x8f0c0e | D:\DEV\macdowsOS tool UI\demos\window-styler | Win32 Ribbon / DirectUI | unsupported |
| 0x230802 | D:\DEV\MC-Service | Win32 Ribbon / DirectUI | unsupported |
| 0x59047c | aiboqiao6/macdowsOS-Widget: 仿 macOS 的第三方桌面小组件 - 个人 - Microsoft​ Edge Beta | Chromium Views | unsupported |
| 0x9a0832 | 设置 | UWP / Windows XAML | excluded |
| 0xc009cc | 设置 | UWP / Windows XAML | excluded |
| 0x200e4 | com.ccswitch.desktop-siw | Win32 | excluded |
| 0xa0204 | Windows 输入体验 | UWP / Windows XAML | excluded |
| 0xa0c8a | Program Manager | Win32 | excluded |
| 0x9b062a | MC-Service 1.20.1 / Java + Bedrock Console | Console host | captured |

## 验证

x64 CTest 12/12，x86 CTest 6/6、完整分发包控制器检查 14/14 通过。当前已接入窗口的绿灯最大化、绿灯还原、黄灯最小化及会话还原全部通过。完整报告及四组截图见 `out/live-v6/release-screenshots/report.md`，按钮记录见 `out/live-v6/input/results.json`，分发包检查见 `out/controller-direct-v6/checks.json`。

此前 frpc Terminal 和旧服务端 conhost 的截图保存在 `out/live-v6/final-screenshots`，旧 HWND 不计入最终构建当前通过数量。服务端 conhost 随后重开为 0x9b062a，已经补测当前构建截图与输入，并追加到最终报告（addedAfterInitialInventory=true）。

## 尚未适配

Edge 157.0.4341.24 使用原生 Views；msedge.dll 的精确 CodeView 标识为 8DE675E4-DC25-455F-4C4C-44205044422E / age 1。对应公开 PDB 返回 HTTP 404，当前没有经过验证的原生控件布局入口。明确跳过，不使用网页 DOM、标题栏或遮罩回退。

当前 3 个 Ribbon 窗口使用 UIRibbon.dll NetUI 树。已通过同版本公开符号和只读探针找到 HWNDElement、AppFrameHelper、OfficeCaption、QAT 及 GetPadding/SetPadding 等入口。原按钮隐藏、内嵌新按钮和绑定还原尚无完整实现，不能把读取控件树计为适配成功。只读研究保存在 out/live-v6/ribbon-probe，未分发为适配器。

软件升级、页面重载、跨屏 DPI 和未列出的框架/版本没有完成验证。当前包只为明确支持的布局接管；自动检测不代表通用兼容。

## 实现修正

QQ 鼠标按下会改变原按钮 CSS 类；现在验证原按钮排列并按 nth-last-child 定位，保留原事件。原 QQ 标识下移，避免覆盖。

控制面板与 Ribbon Explorer 同属 CabinetWClass，按实际子控件和客户区区分；保留原客户区，标题绘制 buffer 限定在原标题区域，显式 alpha 防止壳层颜色相加。

Electron 按 DLL/脚本版本隔离回调，异步布局/还原完成才确认；Terminal 在原 XAML Grid 插入列；conhost 解析实际 GUI 宿主。
