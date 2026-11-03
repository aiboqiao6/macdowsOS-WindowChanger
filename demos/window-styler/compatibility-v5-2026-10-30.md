# v5 按框架适配与当前桌面验证

环境：Windows 11 build 26100，150% / 144 DPI，管理员控制器；未使用 Computer Use。当前构建仅有进程内绘制和原控件布局插件，不包含跟随窗口。

## 当前结果

当前桌面枚举 18 个 HWND，其中 12 个应用窗口、6 个系统/后台/托盘窗口。2 个真实 WPF 应用通过直接嵌入检查，10 个应用窗口尚未适配。未适配和排除的窗口不计入成功。

| 实际软件 | HWND | 框架/运行时 | 验证结果 |
| --- | --- | --- | --- |
| Visual Studio（管理员） | 0x4f07ae | WPF / .NET Framework CLR v4 | 原顶栏嵌入通过 |
| 通用 x86 调优工具（管理员） | 0x2b0f2e | WPF / CoreCLR .NET 8.0.29 | 原顶栏嵌入通过 |
| ChatGPT / Codex | 0x6b0612 | Chromium / Electron | 缺少内部布局适配 |
| QQ | 0x309f2 | Chromium / Electron | 缺少内部布局适配 |
| Edge Beta 157 | 0x59047c | Chromium Views | 缺少内部布局适配 |
| 资源管理器（4 个） | 0x800d9c、0x8f0c0e、0x230802、0xdf0ce4 | Win32 Ribbon / DirectUI | 缺少原控件布局适配 |
| Windows Terminal（2 个） | 0x380574、0x2094a | WinUI 2 / XAML Islands | 原标签行未取得，研究接口尚未完成 |
| MC-Service 控制台 | 0x32160e | ConsoleWindowClass / 代理 HWND | 未向控制台客户进程注入 |
| 设置（2 个）、Windows 输入体验 | 0x9a0832、0xc009cc、0xa0204 | Windows XAML | DWM cloaked，当前没有可见画面 |
| 开始、Program Manager、CC Switch 托盘辅助 | 0xa0cae、0xa0c8a、0x200e4 | 系统/辅助窗口 | 排除 |

应用窗口已保存原始截图；两个受支持窗口另存应用后约 100ms、稳定后和恢复截图。两个应用的三色像素、原按钮隐藏、客户区、外框、恢复几何全部通过，完整画面未见右侧/底部新增留白。截图检查结束恢复原始位置、最小化状态和前台。

截图证据：[逐窗口报告](out/live-v5/final-screenshots/report.md)、[机器可读结果](out/live-v5/final-screenshots/report.json)。应用回执耗时包括创建守护进程：VS 387ms、调优工具 250ms；不是逐帧绘制延迟。移动和布局由目标 WPF 同步完成，不使用定位计时器。

![实际软件前后对照](images/window-injector-v5.png)

## 框架分派与实现

`framework.cpp` 按 HWND 框架特征识别 Win32、Qt、WPF、Chromium、XAML、Ribbon/DirectUI、Console。WPF/Qt 同时检查目标已加载运行时，避免把伪装窗口类当作真实框架。注册项可设置 `Framework=wpf` / `qt` 等；旧插件省略该字段仍可使用。最终是否能修改由目标 GUI 线程中的 Probe 决定，列表里的候选不等于附加成功。

WPF 插件同时支持已运行的 CLR v4 和 CoreCLR。布局策略验证 Visual Studio 的 MainWindowTitleBar / WindowTitleBarButtons，或 Wpf.Ui.Controls.TitleBar 的原按钮容器；通过公共 WPF API 修改模板中的实际对象，不读写私有对象偏移。原按钮容器 Collapsed，现有顶栏 Margin 左侧增加 84 DIP，左侧 Canvas 和三个标准 WPF Button 加入同一个原 Grid，实际标题行高度由原布局决定，没有新增 HWND。

鼠标命中在原 HwndSource 内处理，标准 Button 保留键盘和无障碍调用。操作发回原窗口 WM_SYSCOMMAND。控件高度绑定原顶栏 ActualHeight；缩放和菜单换行由原布局同步更新。保存原 Margin/Visibility 的本地值或绑定；退出、所有者死亡、目标销毁时还原并释放进程句柄。750ms 定时器仅检查所有者存活。

WinUI 研究接口能连接当前 Terminal 的 XAML 诊断服务，但目前只能取得空根节点，无法证实真正标签行及按钮布局。该实验默认 `STYLER_XAML_RESEARCH=OFF`，不进入分发包和自动管理。Chromium/Explorer 仍需要各自真实控件树入口；管理员权限和 DLL 加载本身不能解决内部布局问题。

## 回归

- x64 CTest：12/12 通过，包含 CLR v4 和 CoreCLR 两套原位 WPF 生命周期测试。
- x86 CTest：6/6 通过，包含 CLR v4 WPF 原位生命周期测试；x86 CoreCLR 未实测。
- 分发包 UI/CLI 集成：14/14 通过，包含原生双架构、Qt 两种渲染、WPF 双架构 CLR v4 与 x64 CoreCLR、暂停/恢复和崩溃还原。
- WPF 测试覆盖真正注入、无新顶层/子 HWND、几何保持、24 次移动/缩放、按钮最小化/最大化/关闭、撤销、控制器和守护进程异常退出。
- 历史 DWM/扩展标题栏测试保留作回归，不能算作未知软件原位适配成功。

实际应用只做可逆样式和截图验证；红色关闭按钮的自动测试仅作用于本次自建测试进程。
