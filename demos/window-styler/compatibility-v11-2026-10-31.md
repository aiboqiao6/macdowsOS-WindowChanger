# v11：ApplicationFrame / UWP 原标题区

日期：2026-10-31。后端按框架和原控件树识别，没有应用名、路径、业务控件名称或宿主对象偏移分支。不使用 Computer Use。

## 系统设置

新增后端 262（UWP XAML 原返回按钮容器留白）和 263（ApplicationFrame 原合成标题按钮）。外壳与 CoreWindow 必须属于不同进程、有真实父子关系且内容唯一；框架 ABI、窗口实例、GUI 线程和原结构均验证后才修改。普通子窗口、同进程伪装类名、权限或架构不符均拒绝。

在原按钮的 IDCompositionSurface 绘制三圆点，移动原按钮矩形。原生 reserved metrics 通知原标题文本右移；公开 UWP XAML API 只改变原 NavigationView 小型标题容器的 padding/min-width/width，让返回按钮右移。无新 UIElement、HWND、标题行、位置跟随或绘制轮询。原客户区、外框、DWM 边界保持。

真实鼠标回归发现 ApplicationFrame 最大化后会切换原有输入窗口：两个 ApplicationFrameTitleBarWindow 共用同一 CTitleBar，其中一个初始隐藏。只接管可见窗口会让最大化后绿灯落入旧的关闭命中区。现接管同一原对象的可见与隐藏输入窗口，并处理新建通知；CoreWindow 使用公开 Pointer 事件补齐输入，所有动作交给框架原按钮入口。早期失败记录保留，不计为通过；测试增加 HWND 存活和 Query 检查，失效后取消指针输入。测试期间误触造成设置窗口关闭，已重新打开恢复，并验证修复后窗口持续存活。

附加按内容留白 → 外壳按钮的顺序；失败两侧回滚，未确认撤销的依赖继续跟踪。撤销、控制器退出或所有者失效会还原两进程属性/绑定、输入回调和原按钮绘制。最后撤销禁用框架 detour 并恢复代码字节，保留 trampoline 分配与已加载 DLL，避免悬空返回地址。

## 已完成实测

- 本机 150% 缩放，原三色 FF5F57 / FFBD2E / 28C840。
- 首次附加、稳定、样式回写、主题通知、最大化、还原、撤销均有 GDI 真实截图；客户区与外框不变，无新增子 HWND，撤销几何恢复。
- 黄灯最小化、绿灯最大化/还原：真实鼠标通过，禁用 UIA fallback；窗口仍存活。
- 11 个设置分类页，初始及每页早期/稳定共 23 帧；同时回放宿主写回系统样式，三灯像素与原按钮检查全部通过。
- 新增策略测试：普通子窗口与框架关系不符时拒绝、拒绝后无请求残留、同进程匹配类名不能伪装为嵌入事务。

证据：`out/live-v11/settings-final/result.json`、`out/live-v11/input-final/results.json`、`out/live-v11/views-final/results.json`。分发只收录裁切后的标题区截图。

## Edge 实际状态：未完成

当前 Edge Beta 的 Chromium Views 157.0.4341.24，msedge.dll CodeView 8DE675E4-DC25-455F-4C4C-44205044422E age 1。微软符号服务器对应 PDB 和压缩变体未取得，已保留请求记录。

本轮通过 Chromium 框架源码、窗口 userdata、框架虚表调用与只读反汇编，定位到 HWNDMessageHandler → DesktopWindowTreeHostWin → Widget → RootView 链。尚未验证通用 View 元数据、布局修改、原控件绘制及生命周期入口，没有对该原树写入或注册假成功后端。现预检模块/导出能力，原生 Views 明确显示待适配，不能复用 Electron renderer。

该进展不能算 Edge 适配成功。没有用覆盖窗口、强制系统标题栏或应用专属 BrowserFrameView 偏移替代。

## 限制

ApplicationFrame 当前仅验证框架 CodeView 364A364E-553D-E50F-96EA-7A71CEA2E676 age 1，未知版本拒绝。当前机器的中文布局已验证；其他语言、其他 UWP 标题结构、完整树替换、跨屏 DPI、真实系统主题切换和红灯关闭语义尚需专项验证。只在自有 fixture 测试红灯，不再点击用户窗口红灯。

MinHook 1.3.4 源码、许可证与来源哈希位于 third_party/minhook。旧 v10 包单独保留。
## 打包检查记录

内容版本 d91bb26d6222。隔离旧自动控制器后，x64 CTest 14/14、x86 CTest 6/6。首轮失败受旧自动控制器同时接管测试 fixture 影响，原日志保留 ctest-x64-controller-interference.log。

正式包双架构联测目前 11/16，x64 项目通过；x86 线程 Hook 返回 ERROR_MOD_NOT_FOUND (126)。随后对构建目录、打包目录、旧 v10 DLL 和独立路径作最小注册检查均复现，加载 DLL 本身成功，尚未定位原因。先前 x86 CTest 通过不能代替这次打包联测结果，当前不保证 x86 分发兼容。没有通过删除失败测试或替换为外部窗口绕过。

最终本机 x64 实测：当前 10 个应用窗口（系统设置、任务管理器、Terminal、Visual Studio、WPF 调优工具、Codex、QQ、VMware、conhost、控制面板）黄绿灯操作和撤销 10/10，窗口均存活；使用正式包执行，证据 out/live-v11/input-all-final/results.json。系统设置正式包截图再次确认三色、几何、样式/主题通知、最大化和撤销通过。

管理员自动控制器已切换为 v11，PID 10996；10/10 窗口附加并恢复原位置和状态。Edge 保持原样，明确显示 Chromium Views 待适配。旧 v10 目录与 ZIP 保留，旧控制器已正常退出。
