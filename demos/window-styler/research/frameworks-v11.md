# v11 框架研究记录

本轮实现：ApplicationFrame.dll 的原 CTitleBar 与原 IDCompositionSurface；UWP Window.Current + ICoreWindowInterop + VisualTreeHelper。具体实现及已验证范围见 ../compatibility-v11-2026-10-31.md。

ApplicationFrame CodeView：364A364E-553D-E50F-96EA-7A71CEA2E676 age 1。入口在读取时核验，修改的是框架对象而非应用宿主对象。_ComputeLayoutRects 0x24b40、_PaintButton 0x22210、_PlaceVisuals 0x247d0、GetFrameWindow 0x42350、_OnAccessibilityButtonInvocation 0x5ca90。原按钮合成表面、矩形、可访问性和动作复用；不创建附加窗口。

输入方面，同一 CTitleBar 的多个原 ApplicationFrameTitleBarWindow 可在最大化时切换可见性。需要按实例收集全部输入窗口并处理生命周期，不能只绑定首次可见 HWND。CoreWindow 与外壳之间只发布标量 caption 几何和 HWND lease，不跨进程共享指针。

## 原生 Chromium Views

取得的上游源码包括 ui/views/win/hwnd_message_handler、desktop_aura/desktop_window_tree_host_win、widget/widget、view、ui/gfx/win/window_impl、ui/aura/window_tree_host 与 ui/accessibility/platform/ax_fragment_root_delegate_win。来源为 Chromium 官方 GitHub 镜像 main。它们用于语义核对，不代表与目标二进制完全同版。

只读反汇编进一步核对当前框架的 AXFragmentRootDelegate → HWNDMessageHandlerDelegate::GetNativeViewAccessible → Widget → RootView 路径。已读到 Widget/RootView 非空指针与模块内虚表，相关观察保存在 out/live-v11/edge-object.json。所有这些都是研究结果，尚无布局/绘制写入口、完整生命周期验证或生产后端。

当前版本微软公开 PDB 请求失败，证据 out/live-v11/edge-symbol-check.json。磁盘还有另一修订 .30，其身份也单独检查，不能拿 .30 的地址用于运行中的 .24。

下一步要以框架元数据和标准窗口操作语义定位已有按钮，验证 View 的 bounds/visibility/paint 修改、事件与布局回调、属性回写和撤销。不能直接猜写当前观察到的对象指针或产品布局字段。原生 Views 缺少 Electron Node-API/V8 能力，通用注册器现从控制器只读核验导出能力，避免错误候选。