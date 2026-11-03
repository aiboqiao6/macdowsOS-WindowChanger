# v3 兼容性记录 · 2026-10-30

Windows 11 build 26100，150% / 144 DPI。默认管理员入口；使用原生 API、原生屏幕复制，没有使用 Computer Use。通过主界面的“截图检查全部”入口实际执行。

本次 17 个窗口逐项记录：12 个应用完成截图及几何恢复检查，0 个失败，5 个排除（控制器自身、开始按钮、Program Manager、托盘辅助窗口、DWM 隐藏的输入体验窗口）。12 个应用的按钮中心像素均为 FF5F57 / FFBD2E / 28C840；客户区尺寸保持，守护撤销后外框和客户区恢复，原位置和状态随后恢复。

视觉复核：12 个窗口均对照首次/稳定截图核对原按钮遮蔽及顶部内容。没有发现首次应用后标题缺失、右侧/底部新增大块留白。1 个传统控制台复用原生标题区域；11 个自绘窗口采用下移内容回退，没有把内容区域误判为空白。此结论只覆盖本次窗口和环境。

![实际 Edge 应用前后对比](images/window-lab-v3-edge.png)

## 逐窗口结果

| HWND | 应用 / 类 | 权限 | 最终策略 | 结果 |
| --- | --- | --- | --- | --- |
| 0xa0cae | Button | medium | 排除 | 桌面、任务栏及其按钮不属于应用窗口 |
| 0xa3051a | Macdows.WindowStyler.Controller.v1 | high | 排除 | 截图控制器自身 |
| 0xdf0ce4 | explorer.exe / CabinetWClass | medium | 内容下移 / 预留标题带 | 截图、像素、客户区、撤销几何通过 |
| 0x2094a | WindowsTerminal.exe / CASCADIA_HOSTING_WINDOW_CLASS | medium | 内容下移 / 预留标题带 | 截图、像素、客户区、撤销几何通过 |
| 0x380574 | WindowsTerminal.exe / CASCADIA_HOSTING_WINDOW_CLASS | medium | 内容下移 / 预留标题带 | 截图、像素、客户区、撤销几何通过 |
| 0x2b0f2e | Universal x86 Tuning Utility.exe / HwndWrapper[Universal x86 Tuning Utility;;d56a1159-a1bb-41a4-a19b-19c1883bb2e9] | high | 内容下移 / 预留标题带 | 截图、像素、客户区、撤销几何通过 |
| 0x8f0c0e | explorer.exe / CabinetWClass | medium | 内容下移 / 预留标题带 | 截图、像素、客户区、撤销几何通过 |
| 0x230802 | explorer.exe / CabinetWClass | medium | 内容下移 / 预留标题带 | 截图、像素、客户区、撤销几何通过 |
| 0x32160e | cmd.exe / ConsoleWindowClass | high | 复用原标题区 | 截图、像素、客户区、撤销几何通过 |
| 0x4f07ae | devenv.exe / HwndWrapper[DefaultDomain;;f93aeb5b-31f4-4500-abc3-65ddae51a606] | high | 内容下移 / 预留标题带 | 截图、像素、客户区、撤销几何通过 |
| 0x59047c | msedge.exe / Chrome_WidgetWin_1 | medium | 内容下移 / 预留标题带 | 截图、像素、客户区、撤销几何通过 |
| 0x309f2 | QQ.exe / Chrome_WidgetWin_1 | medium | 内容下移 / 预留标题带 | 截图、像素、客户区、撤销几何通过 |
| 0x950860 | ApplicationFrameHost.exe / ApplicationFrameWindow | medium | 内容下移 / 预留标题带 | 截图、像素、客户区、撤销几何通过 |
| 0x6b0612 | ChatGPT.exe / Chrome_WidgetWin_1 | medium | 内容下移 / 预留标题带 | 截图、像素、客户区、撤销几何通过 |
| 0x200e4 | com.ccswitch.desktop-sic | medium | 排除 | 工具 / 托盘辅助窗口，不作为主应用窗口替换标题栏 |
| 0xa0204 | Windows.UI.Core.CoreWindow | medium | 排除 | DWM 隐藏窗口（其他桌面或系统后台），当前没有可见画面可检查 |
| 0xa0c8a | Progman | medium | 排除 | 桌面、任务栏及其按钮不属于应用窗口 |

## 对照检查要点

- QQ：保留左上标识、独立窗口图标、会话通话/分享工具栏；仅遮蔽右上三个窗口操作按钮。
- 设置：返回按钮与原标题保留。缺少标准系统菜单的 SC_CLOSE 项不再被当成禁用关闭；复拍确认红色按钮正常显示。
- Terminal 两个窗口：标签名和加号保留，原窗口按钮隐藏。
- Visual Studio（管理员）：菜单、工具栏、文档标签保留。
- Universal x86 Tuning Utility（管理员）：捐赠与 Discord 控件保留，原最小化/最大化/关闭区域遮蔽。
- 三个 Explorer：文件菜单、快速访问、功能区和地址区保留。
- ChatGPT：顶部菜单、原有侧栏/面板内容保留。
- 传统 Console：复用原有非客户区，无新增内容预留带。

## 回归证据

x64 完整 CTest 10/10、Win32 完整 CTest 4/4 通过。最后的关闭菜单兼容性修正后，x64 三项 adaptive 检查和 Win32 adaptive 检查再次通过。真实管理员控制器流程 79/79，通过默认接管、暂停、新窗口加入、单项排除、全部撤销和正常退出恢复。

自有模型和真实 Qt/WPF 检查了红色关闭、黄色最小化、绿色最大化/还原，以及最小化/最大化时撤销、随后恢复的完整几何。实际应用截图只做样式应用/还原，没有点击实际应用的关闭按钮。截图流程终止的仅是测试控制器工作进程，用于验证独立守护恢复。

完整原始证据仅保存在本地：`out/visual-check-v3-verified/report.json`、`report.md`、`visual-review.json` 及各 HWND 的 before/after/stable/restored PNG。真实控制器检查在 `out/controller-audit-v3/result.json`。分发包不包含 QQ、终端等应用原图。

## 当前能力边界

尚未实现所有自绘框架内部内容的通用右移。当前批次没有选择紧凑原位路径，大部分窗口仍走用户允许的内容下移/新增标题带回退。原按钮是合成层遮蔽和鼠标拦截，原控件与无障碍元数据未删除；新按钮尚无独立无障碍提供者。

需要注入的回退与目标进程位数必须一致，当前 12 个应用为 x64；Win32 包另行通过自有模型回归，没有宣称对所有 x64 自绘目标兼容。Hook DLL 还原后保持映射直到目标进程退出，当前版本为 8e89ed5424ee。跨屏 DPI、其他 Windows build、全部透明/自绘框架仍待扩展验证。