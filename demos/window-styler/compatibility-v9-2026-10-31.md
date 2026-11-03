# v9 本机兼容记录（2026-10-31）

本轮为 Windows XAML Islands 增加通用原布局后端，实际覆盖 Terminal 与任务管理器。两者共用一个 DLL，按框架 ABI、窗口操作语义、窗口标题及原布局几何发现，不按应用名、业务控件名或宿主对象偏移适配。

环境：Windows 11 build 26100，x64，144 DPI（150%）。未使用 Computer Use；使用 Win32/GDI 合成画面截图与原生输入。所有实际改动均在目标进程、原窗口和原控件树内，没有跟随/遮罩窗口或新增标题行。

## 本轮结果

当前实际窗口：10 个截图与按钮检查通过，0 个验证失败，2 个未适配，5 个系统/后台/辅助窗口排除。数量仅指当前盘点的 HWND，已关闭或未运行的旧窗口不沿用 v8 的通过数。

| 本轮重点 | 框架与方案 | 当前状态 | 附加确认 |
| --- | --- | --- | --- |
| Terminal，0x2094a | Windows XAML Islands，原按钮语义与原 Grid | 截图、真实鼠标、最大化/还原、最小化、撤销通过 | 200 ms |
| 任务管理器，0xb006c2 | Windows XAML Islands + DWM，窗口标题及标题区几何 | 截图、真实鼠标、最大化/还原、最小化、撤销通过 | 278 ms |
| Edge，0x59047c | 原生 Chromium Views | 未取得可验证的原布局 ABI，未适配 | 不计为通过 |
| 设置，0x10b0842 | UWP/ApplicationFrame 外壳 + CoreWindow | 已研究框架标题对象，未完成合成及跨进程内容布局，未适配 | 不计为通过 |

附加时间从启动验证 worker 到 Query 确认，包括进程启动和轮询；不是单独的 DLL 加载耗时。此次截图为附加确认后约 100 ms、稳定后、最大化、还原大小与撤销后。实际窗口不测试红灯关闭；红灯仅在自有 fixture 中验证。

完整窗口还包括 ChatGPT、Visual Studio、QQ、资源管理器 NetUI、VMware、conhost、控制面板与调优工具。四边、圆角、原按钮、三灯像素、客户区、外框、无新增子 HWND、撤销及输入记录见 [逐 HWND 截图报告](out/live-v9/release-screenshots/report.md)、[原始指标](out/live-v9/release-screenshots/report.json) 与 [按钮检查](out/live-v9/input-final/results.json)。人工查看了完整窗口、标题对比及正常/最大化/还原画面；没有发现新的右侧/底部空白、原按钮残留或三灯重叠。

## XAML 布局与输入

纯 XAML 隐藏原操作组，在原 Grid 为三灯预留 84 DIP，保存列属性和绑定，并保留原行高，避免 Collapsed 后正文上跳。混合标题保持原控件父子关系、Loaded/Unloaded 生命周期和原列索引，使用 Grid.Padding 腾位；原标题行内的三灯使用原不透明 Brush 或框架 ThemeResource，避免合成表面 GetPixel 返回黑色。真实 WS_SYSMENU 隐藏原 DWM 按钮，样式只读兼容层让宿主继续读取原布局条件。

原生拖动窗口可能盖在 XAML 上方：用同线程、几何与 HTCAPTION 发现，只接管三灯命中区域，排除 Island 输入子窗口；其他位置继续交给原过程。真实鼠标验证没有使用 UIA fallback。XAML Button 的键盘与可访问性保留。

自建 fixture 覆盖纯 XAML 和原生/XAML 混合布局；混合 fixture 自带原生拖动窗口并检测意外 Unloaded。验证首次几何、24 次连续移动/缩放、黄绿灯实际输入、最大化/还原、精确撤销、控制器与 guardian 异常退出、重附加，以及仅关闭 fixture 的红灯。

## 构建与限制

当前运行时 revision：`bbb2c79c2c58`。x64 CTest 14/14，x86 CTest 6/6；日志位于 `out/live-v9/ctest-x64-final.log`、`ctest-x86-final.log`。管理员清单、x86 helper 和原有后端保持沿用。

框架 ABI 当前仅验证 Windows.UI.Xaml.dll CodeView `A5E5662D-B273-6140-B633-BC2D198B5A28` age 1（本机 10.0.26100.1742）。未知版本拒绝内部入口；公开 diagnostics 无法取得原树时保持原样。动态标题行高、运行中切换主题、跨屏 DPI、框架升级仍未验证。不能据本机两个宿主声称所有 XAML 布局都兼容。

实验中的容器替换和 InputSite subclass 曾导致任务管理器异常退出，均已撤回；最终方案不替换原容器、不 subclass InputSite。原因与未完成框架的具体入口见 [框架研究记录](research/frameworks-v9.md)，失败版本不进入分发包。

![原标题行直接嵌入对比](images/window-injector-v9.png)

分发包管理员控制器 16/16 检查通过，覆盖 Qt、WPF、纯/混合 XAML 原布局、x64/x86、自动接管、暂停/恢复、CLI lease 到期与控制器异常退出撤销。记录：out/controller-direct-v9/checks.json。

最终管理员 v9 控制器已启动，当前 10 个已验证窗口全部确认接管，启动检查后恢复了各窗口原位置及最小化/最大化状态。记录：out/live-v9/startup.json、running-inventory.json。
