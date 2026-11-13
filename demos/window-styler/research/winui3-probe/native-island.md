# 独立 WinUI 标题岛屿实验

v42 通过 `winui3-native.cpp` 共用此实现，正式注册为 Adapter288；独立研究构建仍保留。它不按应用名称、路径或软件名单选择目标；注册表要求精确 Microsoft.UI.Xaml CodeView，进程内还要求 x64、144 DPI、LTR、正常窗口、原生标题、唯一且完整填满客户区的 XAML 岛屿。记事本的多个岛屿不满足条件。ABI、源窗口或布局不匹配时拒绝。

## 布局与恢复

原生 GDI 兜底不能可靠绘制到真实 WinUI 合成窗口。这个实验通过公共 AppWindow/OverlappedPresenter API 保留边框并移除原生标题，再在新获得的客户区顶部创建独立 DesktopWindowXamlSource，使用真实 XAML Button 绘制三灯。原岛屿只改变相对于客户区的位置，维持原尺寸和屏幕位置；正式实验不改写、包裹或卸载原 Content，不修改其 Margin 或 Row。

NativeIslandFrame 还保留了包裹原根和修改原根 Margin 两种对照模式，仅供复现失败实验。WinUI3NativeIslandHost 始终使用独立标题岛屿模式；两个对照模式不能作为实际画图适配方案。

最大化时扣除不可见缩放边框对应的标题空间。标题拖动与三灯输入区域通过 InputNonClientPointerSource 设置，保存并恢复原区域。前置原生输入过滤器禁用旧非客户区按钮鼠标消息，标题栏查询报告旧按钮隐藏、不可用、离屏且矩形为空。新三灯仍走原 HWND 的系统命令，执行前检查原窗口能力。

标题跟随原根的 ActualTheme 和 Background 属性变化，撤销时注销观察回调。三灯不进入 Tab 顺序，点击不获取原内容的键盘焦点。客户区宽度少于 84 DIP 时拒绝附加；附加后宽度不足或高度无法容纳标题时自动撤销。当前 144 DPI 边界是 126 原生像素。

撤销开始时按值比较接管结束时的标题属性，合并宿主后来更新的配色、图标、标题高度和主题，包括把非空配色清为 null；恢复失败重试时不重复采样自身恢复过程。随后关闭自建标题岛屿，恢复 presenter 的边框和标题，调用 TitleBar.ResetToDefault 恢复原生标题绘制，再恢复非空配色及有差异的选项。非默认选项需要临时进入扩展标题模式才可设置，设置后取消扩展。最后恢复原输入区域和岛屿位置。原区域为空时使用 ClearRegionRects。最小化时保留原岛屿 viewport 尺寸，仅恢复位置；不更改系统或应用主题。

研究控制器限时运行，owner 进程消失或宿主替换原 Content 后自动撤销。恢复失败不返回成功，不删除仍需恢复的会话，保留定时器重试。初始化期间遇到释放或窗口销毁时延后清理，避免 Apply 调用栈中的 frame 被重入删除；恢复期间也阻止重复释放。宿主替换的新 Content 不会被旧根覆盖。

保留原 DesktopChildSiteBridge，在读取源 Content 或调整桥接位置前检查 IsClosed。宿主关闭旧 DesktopWindowXamlSource 后，直接读取其 Content 可能触发框架访问冲突，不能依赖 C++ 异常捕获。旧桥接关闭后不再访问该源；只有旧桥接已关闭才在恢复原生标题期间转交最终 WM_SIZE，让宿主调整新建岛屿，避免新控件沿用扩展客户区的高度而发生位置偏移。旧源仍存在时由保留的桥接调整，不重复触发宿主布局。恢复重入的 Resize 只调整正文，不访问已关闭的临时标题岛屿。

独立标题源同样保留其 DesktopChildSiteBridge。宿主通过公共 Close 关闭标题后，所有权和输入就绪查询先检查该桥接，Resize 将事务标为失效并返回 RO_E_CLOSED，恢复跳过已关闭标题的 Content/Close。恢复过程重入时，仅在桥接独立确认关闭后接受 RO_E_CLOSED；其他错误继续保留会话并重试。自有窗口诊断曾在旧版标题关闭后的 Content 查询中等待 15 秒未返回；这次观察不是访问冲突的证明。

公开 XAML 调用期间重入的 WM_CLOSE 或 SC_CLOSE 保存原消息及参数，最外层调用返回后再投递。宿主处理关闭前先完成撤销；WinRT 引用释放期间仍保留 releasing 状态和消息拦截，使重入查询不再访问正在关闭的 Content。恢复失败时保留关闭请求并重试。初始化完成前重新确认原根所有权，未完成 Apply 的嵌套 Detach 不提前确认成功。

## 构建与自有窗口验证

正式构建默认启用 `STYLER_WINUI3_NATIVE`，需先按主 README 准备投影头文件。正式 `WinUI3NativeIslandHost` 显式调用 Automatic，并确认最终 Adapter288；它是验证程序，不进入运行包。各契约脚本的 HostDirectory 可指向正式配置目录，真实画图脚本另加 `-ProductionRegistry` 并把 HostDirectory 指向含 Debug/Release 的构建父目录。`test-winui3-auto.ps1` 则直接启动实际控制器，在 GUID 隔离设置与自有 PID 范围验证默认接管和托盘；同权限测试不等于高权限自启验证。

正式 x64 构建另含 `WinUI3ClosedTitleTests`。`ctest --test-dir <构建目录> -C Debug -V -R ^winui3_closed_title_` 及对应 Release 命令执行深浅色的三层关闭回归：共享事务、从 adapters.ini 加载实际模块的公开适配 ABI、外部验证主机的 Automatic 跨进程注入。窗口不激活、不使用全局键鼠或截图；跨进程模式仅注入自身 fixture。检查关闭后的安全查询、恢复及租约清理、原 Content/HWND/正文 viewport/几何/style/标题、TextBox 单次 Loaded 且从未 Unloaded，以及再次附加和撤销。取得标题源的诊断读操作也受固定框架 ABI 与入口字节约束，Debug 使用与该 ABI 一致的 release CRT；这不是任意 WinUI 版本的源枚举入口。此组检查不能替代实际点击、输入法或整窗像素矩阵。

使用已准备好 out/winui3-sdk 的 x64 research/framework-probe 构建目录：

~~~powershell
. ./demos/window-styler/development-tools.ps1
& (Get-DevelopmentTool cmake) --build ./demos/window-styler/out/framework-probe-x64 --config Release --target WinUI3NativeIslandFixture WinUI3NativeIslandHost --parallel
& ./demos/window-styler/research/winui3-probe/test-native-island.ps1 -TitleIsland
& ./demos/window-styler/research/winui3-probe/test-native-island-lease.ps1
& ./demos/window-styler/research/winui3-probe/test-native-island-boundaries.ps1
& ./demos/window-styler/research/winui3-probe/test-native-island-initialization.ps1
& ./demos/window-styler/research/winui3-probe/test-native-island-text.ps1
& ./demos/window-styler/research/winui3-probe/test-native-island-rebuild.ps1
& ./demos/window-styler/research/winui3-probe/test-paint-document.ps1 -Configuration All
~~~

Debug 时指定各脚本对应的 Debug FixtureDirectory、HostDirectory（第一个脚本不需要 HostDirectory）和独立 OutputDirectory。UI 测试串行运行。第一个脚本在自有深浅色窗口中验证几何、正文像素、原标题文字和配色恢复、标题主题、旧按钮禁用、缩放、最大化和三灯真实点击。第二个脚本通过外部研究控制器验证正常退出、控制器进程异常退出、最小化时控制器退出、根节点替换、宿主关闭和应用内部主题切换。

外部验证主机现由 `native-island-host.cpp` 包装，及时刷新管道输出。`host-readiness.ps1` 保留完整日志，并在主机确认 Session::Apply 成功、选中 Adapter288 后才允许测试继续，避免看到租约就提前撤销。截图用 `capture-guard.ps1` 在等待后核验前台和 Z 序遮挡。`capture-backdrop.ps1` 在自有测试窗口下方放置真实、不激活的纯色辅助窗口，固定 GetWindowRect 所含透明缩放边框背后的内容；不修改目标属性，不裁剪、遮罩或改写截图像素，结束时销毁辅助窗口。背景只是验证工具，不进入运行包或被注入进程。新增背景路径仍需完成其全部矩阵，不能沿用添加前的截图结果。

第三个脚本通过外部研究控制器执行 107 项边界检查，也可指定 -Scenario narrow、keyboard 或 palette。--boundary-sizes 自有窗口仅放开 WM_WINDOWPOSCHANGING 的最小尺寸约束，不改非客户区计算和原按钮能力，以实际到达 125/126px 边界。--interactive 使用显式模板的 XAML Button 接收 CharacterReceived，检查实际字符输入、Tab/Shift+Tab、Enter 及绿灯点击后的焦点；这不是 TextBox、文本编辑或输入法验证。标题属性测试检查 12 项颜色、清空与更新、图标/高度/主题，并把撤销后的完整窗口 PNG 与独立未注入窗口逐字节比较。脚本不裁剪或屏蔽差异。所有脚本只关闭自己创建的 fixture，异常退出测试只终止自己创建并校验路径的研究控制器。

第五个脚本使用 --text-editor 的标准 WinUI TextBox，未替换其默认模板；fixture 提供 IXamlMetadataProvider 和 XamlControlsResources。通过真实 SendInput 验证全文及选区替换、Delete/Backspace、多行、Unicode 字符、Tab/Shift+Tab、Enter，以及跨附加/撤销的 Ctrl+Z/Ctrl+Y。脚本比较文本、选区、焦点、CanUndo/CanRedo、TextChanged 和 Loaded/Unloaded 次数；深浅主题各执行正常撤销和控制器异常退出，共四种场景，每种先运行三个独立且从未注入的原生对照窗口。

没有编辑的接管/撤销要求完整窗口 PNG 与原图一致。有编辑、最大化和主题切换的事务要求最终完整 PNG 与执行同样编辑、原生最大化/还原及主题切换的原生对照一致；不裁剪、遮罩或放宽像素比较。标准 TextBox 的圆角边缘在原生主题/尺寸循环后也可能重新栅格化，因此保留最初图像，但不把它冒充完成该循环后的原生参考。每种场景还要求原 TextBox 始终只加载一次、从未卸载。输入使用正确扫描码和扩展导航键标志，不更改键盘布局或剪贴板；Unicode SendInput 不等于 IME 组合输入验证。输出目录已有 checks.json 或 result.json 时拒绝覆盖。

真实窗口的研究入口为该独立目录中的 WinUI3NativeIslandHost.exe，参数为 HWND 和 3 至 30 秒持续时间。该命令会实际修改目标窗口；不能把一次 Probe 或会话成功当成完整适配验证。软件库存、实际窗口截图及私有测试只保留在忽略的 out。

第六个脚本在相同 HWND 内分别替换完整根和标准 TextBox、立即重建 DesktopWindowXamlSource、关闭旧源后间隔 750ms 异步创建新源。深浅色均运行独立原生对照和研究接管窗口；旧控件必须只卸载一次，新控件的文本和历史不能被旧会话覆盖。检查旧会话退出、属性清理、新岛屿尺寸及正文位置、再次接管、绿灯最大化/还原、Ctrl+Z/Ctrl+Y、正常和异常恢复。重建后及最终状态均与原生对照比较完整 PNG，不裁剪、遮罩或增加容差。可指定 -Scenario root、source 或 gap。

## 多岛屿 TabView 插槽研究

`tab-strip-slot.h` 是独立的研究事务，没有注册为正式后端。它在原 TabView 的公共 TabStripHeader 中预留 84 DIP 并创建三灯，不包裹、卸载或替换原标签根，也不移动任何岛屿 HWND。原生 EDIT、菜单、状态栏和滚动条保持原布局。三灯继承同一岛屿的主题与透明背景，不跨 XamlRoot 复制颜色，不进入 Tab 顺序，也不获取交互焦点。

附加要求原 Header 为空且没有 binding/template、LTR、已加载、同一 GUI 线程和相同顶层 WindowId；当前研究另外要求 TabView 宽至少 220 DIP。这只是实验保护条件，不代表已经验证所有 220 DIP 以上布局。撤销区分原来的 UnsetValue 与显式 null；宿主后来设置 Header 或 binding 时保留其新值，包括 binding 的结果恰好仍为原三灯对象的情况。按钮只保留共享命令状态与控件弱引用，每次操作重新核验插槽所有权、窗口身份和系统按钮能力；撤销后立即失效。集成方需在布局变化后显式调用 Refresh，窄宽、失去归属等情况撤销；目前没有正式自动接管租约或后台轮询。

`WinUI3TabStripTests` 只创建自身五个真实 XAML 岛屿与原生 EDIT，以 SW_SHOWNOACTIVATE 在底部显示，通过定时器执行。先加载实际只读诊断模块，确认它通过公共适配 ABI 读出五个岛屿及空插槽元数据、不附加、不留下租约或采集文档和标签文字，再验证事务。原根和两个 TabViewItem 必须只 Loaded 一次、从未 Unloaded，原 Content、选中对象、各 HWND 几何、正文和原生 undo 均保留。深浅主题、宿主切换主题、实际布局避让、窗口放大/还原、200 DIP 窄宽拒绝、Header/template/binding/RTL 拒绝、不同 HWND 拒绝以及保留旧控件均有后台检查。

按钮测试调用公共 ButtonAutomationPeer 的 Invoke，经过真实 Click 回调并确认投递 SC_CLOSE/SC_MINIMIZE/SC_MAXIMIZE；自有窗口子类拦截这三条消息，不真正关闭、最小化或最大化。它验证命令路径和失效保护，不证明真实鼠标命中、拖动、系统动作、键盘或 IME 已通过。最终通过 EM_UNDO 验证原 EDIT 确实能恢复接管前文档，不只检查 EM_CANUNDO。

测试还调用公共 RenderTargetBitmap.RenderAsync/GetPixelsAsync 导出原标签根的完整框架位图，验证三灯确实绘出，并在正常撤销后逐字节比较整个 BGRA 缓冲区和原尺寸。分别为原图、附加图及恢复图独立采样，连续 300ms 完全相同才认为稳定；不寻找与参考恰好相同的帧，不修改像素、关闭原动画或增加容差。最初失败图捕获了原 TabView 尚未结束的入场动画，原始位图和失败记录保留。每次采样保存独立的原始 BMP 到忽略的构建目录，稳定前后均保留。这个输出只属于原标签根，不包含其他岛屿、原生 EDIT、非客户区或屏幕背景；它不是完整 HWND 截图。

此事务没有隐藏禁用原生标题按钮，也没有调整原非客户区输入区域；测试明确断言 native style 不变。记事本的标签岛屿还带有宿主自己的水平偏移和动态布局，原生按钮隐藏、左侧最终定位、输入区域及完整撤销必须另行完成。不能将该 Header 原型当成记事本适配完成，不能并入正式自动接管或运行包。

~~~powershell
. ./demos/window-styler/development-tools.ps1
$cmake = Get-DevelopmentTool cmake
$ctest = Join-Path (Split-Path $cmake) ctest.exe
foreach ($configuration in @('Debug', 'Release')) {
    & $cmake --build ./demos/window-styler/out/framework-probe-x64 --config $configuration --target WinUI3TabStripTests --parallel 3
    if ($LASTEXITCODE) { throw 'TabView research build failed' }
    & $ctest --test-dir ./demos/window-styler/out/framework-probe-x64 -C $configuration -V -R '^winui3_tab_strip_'
    if ($LASTEXITCODE) { throw 'TabView background contract failed' }
}
~~~

## 验证边界

test-native-island-initialization.ps1 在自有窗口的标题模式 setter 回调内、租约属性发布前，确定性触发根替换、嵌套撤销、WM_CLOSE、SC_CLOSE 和取消关闭，共 97 项检查。包含正常退出码、回调返回时 HWND 存活、临时岛屿清理、属性及几何恢复、新根保留和再次接管；取消关闭及嵌套撤销还比较完整窗口 PNG。它不模拟直接 DestroyWindow、任意线程强制销毁或框架调用自身失败。

test-paint-document.ps1 在真实画图中使用自有 PNG、原铅笔和真实输入，验证接管前后及恢复后的绘制、保存、Ctrl+Z/Ctrl+Y、绿灯最大化/还原与原编辑历史。Debug/Release 各有正常及异常恢复，每组先运行两个从未注入的原生对照，共 12 个进程、622 项。保存文档比较解码后的 ARGB 原像素；本地正文比较完整 PNG，最终整窗必须精确匹配注入前建立的两个完整原生参考之一。画图自身可产生工具栏边缘 2 像素的不同原生栅格化，完全未注入重放已经复现；不使用遮罩或数值容差。已有画图进程时拒绝测试，-WithoutInjection 可作纯原生诊断，-OutputDirectory 必须为新目录。

全机软件、多岛屿、全部 DPI、跨屏、实际应用的完整深浅主题矩阵、IME、实际应用宿主重建和更多动态标题行为仍需分别验证。画图当前深色 PNG 编辑和标准 TextBox 测试不足以证明全部应用、工具或对话框已适配。本机只有一块 144 DPI 显示器，本轮未改变系统缩放或任何已有应用的主题。最新结果见 [v42](../../verification-v42.md) 和 [v41](../../verification-v41.md)，宿主重建及失败记录见 [v40](../../verification-v40.md)，此前实验见 [v39](../../verification-v39.md)、[v38](../../verification-v38.md)、[v37](../../verification-v37.md) 和 [v36](../../verification-v36.md)。
