# Window Injector · 原树三灯 v30

当前 MFC/BCG、Office NetUI、TXGui 的已验证有限 ABI profile 已加入主控制器和完整包。TXGui 原树左侧三灯、原 D2D/输入、持续附加及异常恢复已整合；使用和边界见[原树三灯说明](original-frameworks.md)、[v30 验证](research/frameworks-v30.md)。下文 v13…v29 保留为历史过程；未知版本/布局的兼容范围不扩大。

v33 默认自动接管所有受支持窗口，不设软件黑白名单；补充原按钮输入禁用、原配色跟踪和旧式 DPI 标题裁剪修复。本机全部软件及亮暗主题验证仍未完成。[运行说明](release-v33.md) · [本轮证据与缺口](verification-v33.md) · [v32 验证](verification-v32.md)。使用 `out/WindowChanger-v33.zip`，保留 `x86` 目录。手动还原仍暂停全局自动接管，重新开启后对所有支持窗口重试；保留实际 ABI、原布局、所有权和撤销检查。源码仓库：[macdowsOS-WindowChanger](https://github.com/aiboqiao6/macdowsOS-WindowChanger)。

v33 新增 Avalonia 11.3.12 / FluentAvalonia 2.5 的 x64 原标题树后端；亮暗主题、原按钮禁用、真鼠标操作和异常恢复在自有窗口验证。XAML 合成窗口继续要求匹配的原树布局，不能仅因存在非客户区标题而使用 GDI 兜底；新增实际客户区像素保持的拒绝测试。微信内嵌 Qt、Picker 及更多业务窗口仍待适配。

通用 Windows 进程内窗口注入 demo。默认请求管理员权限，统一处理 x64/x86、目标 GUI 线程、探测、附加确认、输入和撤销。后端按框架能力和原控件树选择，不按应用名称、路径或产品版本分派。

**通用框架已经落地，全部窗口兼容尚未完成。** v9 增加 Windows XAML Islands 的纯 XAML 与 XAML/DWM 混合原标题布局。Terminal、任务管理器使用同一个后端；不按应用名、业务控件名或宿主对象偏移选择。v11 增加 ApplicationFrame/UWP 原合成标题按钮和跨进程内容留白；本机系统设置已实测。v12 增加 Chromium Views 原按钮、原布局和 PaintCanvas 后端，当前 Edge x64 已实测；现代 WinUI 3 仍未完成。候选窗口、DLL 载入、Probe 通过都不算实际绘制成功。


v10 修复宿主刷新后原按钮重新出现：XAML/DWM 和 NetUI 共用原生样式写入拦截，在 `WM_STYLECHANGING` 提交前保持真实按钮隐藏，兼容层保留宿主需要的样式视图。XAML、WPF、Qt 对已识别按钮的可见性和标题留白进行事件驱动维护。Win32/NetUI 绿灯使用标准最大化/还原 API，避免宿主把 `SC_MAXIMIZE` 解释为无标题栏全屏。全部修改按框架能力选择，没有应用名分派，也没有增加定位轮询。

v11 按 ApplicationFrame + CoreWindow 框架关系执行两进程事务，复用原按钮表面、原输入 HWND 与原返回按钮容器。未知 ABI 拒绝，任一步失败撤销两侧。控制器预检现在同时核验注册后端的模块与导出能力，原生 Views 不再误列为 Electron 候选。

v12 通过标准窗口操作语义发现原 Views 按钮组，在原绘制管线画三灯，并让原顶部内容行右移。原布局进入/退出执行几何事务，避免最大化后标签消失。未知 CodeView 拒绝，详情见 [v12 检查记录](compatibility-v12-2026-11-01.md)。历史结果见 [v11 检查记录](compatibility-v11-2026-10-31.md)；[v10 检查记录](compatibility-v10-2026-10-31.md) 保留历史证据。
![Edge 原顶部行内嵌三灯的真实截图](images/window-injector-v12.png)

v13 增加同一个 Chromium Views 后端的第二个 ABI profile，当前 Chrome 与 Edge 均通过真实点击和恢复检查。补充最小尺寸/可见性事务和首次隐藏还原按钮的几何，修复绿灯消失及调整尺寸后宽度累积。WinUI 3、MFC/BCG、Office NetUI、TXGuiFoundation 仍未完成；最小化时也按框架明确拒绝原生兜底。本轮 WinUI 3 实验造成照片与记事本退出，已停用故障入口并限制研究加载器仅接受自有 fixture。完整范围和故障见 [v13 检查记录](compatibility-v13-2026-11-01.md)。

![Chrome 原标签行内嵌真实截图](images/window-injector-v13.png)
## 使用

Visual Studio 2022 工程入口、Debug/Release 与 x64/x86 构建和代码清理命令见[根目录说明](../../README.md)。新构建输出使用 `out/vs-x64` 和 `out/vs-Win32`，以下历史报告路径保持原样。

MFC/BCG、Office NetUI、TXGui 的后续对象/原树研究见 [v15 记录](research/frameworks-v15.md)。本轮有新的只读入口和验证工具，尚未增加这三类的正式布局适配。

后续 [v16 实验](research/frameworks-v16.md) 已在本机 BCG 构建跑通原顶栏左侧三灯、原鼠标路径和自动恢复；Office 已读到原操作语义，TXGui 独立原树事务 50 项检查通过。三类均未加入正式自动接管范围，实验的限制和 VS 工程入口见记录。

[v17 研究](research/frameworks-v17.md) 完整校验 Office 的 1,896 个原 Node、唯一活动系统按钮组及 49 项属性修改/精确回滚；TXGui 新增真实 SysButton 与三个内部按钮的 88 项契约检查。两者仍未完成左侧三灯布局或实际绘制验证。

[v18 实验](research/frameworks-v18.md) 已在本机 Office 起始页完成原树左侧三灯绘制、隐藏原按钮、消息路径最大化/还原/最小化与撤销，有限事务 124 项检查通过，验证结束自动恢复。持续附加与更多 Office 窗口类型尚未完成。TXGui 已确认原系统事件映射，当前可见标题按钮仍待关联。

WinUI 3 的后续研究已形成独立原树演示：`out/WinUI3-framework-demo/WinUI3Demo.exe`（压缩包 `out/WinUI3-framework-demo.zip`）只启动并修改自有测试程序。当前 92 项 fixture 检查通过，MFC/BCG、Office NetUI、TXGui 仍未完成。它尚未替换 v13 的实际应用兼容范围，详见 [四类框架研究记录](research/frameworks-v14.md)。

![WinUI 3 原树原型的真实截图](images/winui3-framework-prototype-v14.png)

实验包：`out/WindowInjector-v13.zip`。本轮 Edge 按 x64、150% 缩放、水平标签验证；线程 Hook 返回 126 时使用目标内的短期线程引导，详见 v12 检查记录。运行 `WindowStyler.exe` 自动请求管理员权限，并接管通过进程内检查的当前窗口。最小化窗口在恢复后才进行首次 Probe，避免零尺寸控件树被永久标记为不支持。“暂停并全部还原”撤销修改并暂停自动接管；退出或控制器终止后由目标内的生命周期监测恢复。

```powershell
.\WindowInjector.exe --list
.\WindowInjector.exe --apply 0x123456 --duration 30
.\WindowInjector.exe --all
.\WindowInjector.exe --detach 0x123456
.\WindowInjector.exe --restore-all
```

命令行在期限结束、Ctrl+C 或会话结束后还原。完整包包含 x86 辅助进程；不同架构分别在正确位数进程中注入。管理员权限用于访问同一用户的普通/高权限窗口，不能绕过受保护进程或安全桌面。

[v19 实验](research/frameworks-v19.md) 已将本机 Office 起始页推进为持续原树三灯：Release/Debug 37 项检查通过，包含状态切换后的原绘制、缩放、重复撤销、所有者窗口失效及控制器退出恢复。TXGui 的 21 次原生命中查询定位了三枚真实顶部按钮，只读契约 17/17；操作语义和布局仍待完成。正式控制器范围不变。

[v20 实验](research/frameworks-v20.md) 补上 Office 按下/捕获期间的取消、撤销和控制器异常恢复，Release/Debug 55 项检查通过。TXGui 的 92 个真实按钮事件容器读取完整，顶部三枚按钮的原点击链已解析到宿主回调；只读检查 22/22、独立 SysButton 契约 157/157。通用操作语义选择与 TXGui 三灯布局仍未完成，正式控制器范围不变。

[v21 实验](research/frameworks-v21.md) 已通过 TXGui 原提示和原命中选择，沿原鼠标消息路径验证最大化、还原、最小化（Release/Debug 25 项通过）；没有触发关闭按钮，TXGui 三灯布局/绘制仍待完成。Office 新增宿主能力改变时保留最新请求并撤销，最终 60 项检查通过。验证器修复最小化窗口收尾时被重新激活的问题，四种配置的自有位置恢复契约均通过。两类实验均未加入正式自动接管。

[v22 研究](research/frameworks-v22.md) 在自有进程中验证了 TXGui 原生画布和实心三灯像素，Release/Debug 均 49/49，包含抗锯齿、裁剪、平移、透明混合、状态保持和资源释放；研究回归各 5/5。确认原 `GeoRoundRectEx` 仅画轮廓，实心圆使用原 `FillRoundRectEx`。本轮未修改用户进程；原窗口绘制接入与左侧布局仍待验证，不能视为 TXGui 实际适配完成。

[v23 研究](research/frameworks-v23.md) 已在自有真实 GF 对象上验证原 StackPanel 左侧 84 像素留位与撤销（Release/Debug 各 42/42），以及原分派器触发绘制过滤器、裁剪、移除和回调中自撤销（各 28/28）；研究回归各 7/7。未向用户进程注入本轮代码；真实窗口的自动绘制、可用祖先布局与异常恢复仍待验证，正式控制器范围不变。

[v24 实验](research/frameworks-v24.md) 已接到真实 TXGui 三枚原标题按钮的原 renderer 自动绘制通知，Release 两轮 / Debug 一轮各 39/39，原树和窗口状态恢复。确认实际 graphics 是原 D2DRenderTarget，不能直接使用此前 GDI 夹具的画布布局；也读到了左侧原容器空间不足的实际边界。本轮观察器只透传，不绘图、不发送按钮输入；D2D 三灯绘制与真实左侧布局仍待验证，正式后端未启用。

[v25 研究](research/frameworks-v25.md) 在自有隐藏 HWND 上通过原 factory 创建与真实按钮同型的 D2DRenderTarget，并通过原 compatible bitmap target 验证三色实心圆、裁剪、平移、alpha/状态恢复与资源释放；Release/Debug 各 58/58，研究回归各 9/9。修正离屏回读的 interop DC 配对释放与原 bool 返回类型，失败记录保留。未在用户窗口绘图或改布局，真实回调接入与左侧空间分配仍待完成。

[v26 研究](research/frameworks-v26.md) 已在真实 TXGui 三枚原标题按钮的原内容绘制阶段替换为 D2D 三灯，保留原右侧位置和布局；Release 两轮 / Debug 一轮均 48/48，撤销后完整标题按钮区域逐像素恢复，绑定和几何保持。该轮 512 项原树记录是截断前缀，完整性结论由 v27 更正并重跑。共享回调/原 D2D 契约各 79/79；早期绘制/采样/动画失败记录保留。[实际短时效果](images/txgui-d2d-v26.png)。左侧空间分配、持续会话和异常恢复仍待整合，正式后端未启用。

[v27 研究](research/frameworks-v27.md) 补齐原树读取和独立完整性检查，真实树 1,075 项/20 层，只读 27/27，并在完整原树下重跑原位置绘制和逐像素撤销。左侧布局只读候选可容纳 84 DIP；自有原容器迁移契约 Release/Debug 各 21/21，尚未在用户窗口执行迁移。研究 x86 两种配置各 11/11、x64 各 3/3；未使用 computer-use，正式后端未启用。

[v28 研究](research/frameworks-v28.md) 验证原 Element 与 Frame 的两条独立顺序，通过原 leaf setter 精确恢复，不再移除/重插无关兄弟。自有契约两种配置各 40/40、迁移契约各 22/22；真实原窗口短时顺序事务 Release 两轮/Debug 一轮各 10/10，父关系、几何、绑定和原过滤器保持，外层 33/33、26/26。底部原滚动子树的外层差异在纯只读对照中也出现，诊断保留，不写回动画位置。研究 x86 各 12/12、x64 各 3/3。尚未扩展标题 Frame 或把三灯迁到左侧，正式后端未启用。

[v29 研究](research/frameworks-v29.md) 已用原 padding/margin 和原重排短时扩展真实已有标题 Frame，原高度、原内容和标题按钮的实际位置不变；Release 两轮/Debug 一轮均 18/18，左右局部像素在扩展中及撤销后逐点一致，原树 1,075 节点及完整布局记录恢复。自有契约两种配置各 18/18，研究回归各 13/13。初次属性未生效、位置/尺寸接口、圆角取样及 DIP/原生像素比较失败均保留并修正；没有增加新标题带，仍未把三灯迁到左侧或启用正式后端，未使用 computer-use。

## 统一流程

1. 控制器检查顶层 HWND、位数、权限和结构约束，提交 Automatic。
2. 目标 GUI 线程依注册顺序验证已加载模块和导出能力，不加载缺失的框架运行时。
3. 各后端只读 Probe 原控件树。拒绝后尝试下一个；异步根枚举限时重试。
4. 第一个通过的后端保存原状态并 Attach。Attach 失败回滚并终止，不在部分修改的树上继续尝试。
5. 原布局完成后才确认 Query。记录实际后端 ID，按所有者生命周期撤销。

跨进程线程 Hook 返回 126 时，在目标内注册短期线程 Hook，再将附加消息送入原 GUI 线程；完成后移除引导 Hook。工作线程只负责引导，框架控件仍由原 GUI 线程修改。

没有按应用名称选择后端的分支。不同渲染树需要不同后端：Win32 消息、WPF 依赖属性、Qt 布局、Electron DOM 和 NetUI 元素不能共用一个布局修改函数。框架 ABI/能力检查仍然必要，通用架构不等于任意版本、任意布局都可以无条件写入。

## 后端

| 能力 | 通用发现与修改 |
| --- | --- |
| 原生 Win32 标题区 | 在已有非客户区绘制，保持客户区；不增加标题行 |
| WPF CLR v4 / CoreCLR | 系统命令、可访问语义和共同按钮容器识别；向原 Grid 插入按钮并为原行预留 84 DIP，恢复属性和绑定；不使用 VS/Wpf.Ui 特定控件名 |
| Qt 6.11.1 x64 Widgets / Quick | 标准 QTabBar/QQuickItem、原布局与窗口操作语义；原 backing store/场景绘制，恢复可见性、padding 和 QML 绑定 |
| Electron | 在已加载模块中发现 Node-API/V8 导出；验证 V8 ABI major 13/15；根据 aria-label/title/data-action 和几何识别原按钮，或使用公开 title overlay 与原 drag 行；没有 QQ/Codex 专用 CSS 选择器 |
| Chromium Views x64 | 通过已加载模块 CodeView、框架函数指令、对象生命周期和标准 HTMINBUTTON/HTMAXBUTTON/HTCLOSE 发现原按钮组；移动原按钮与顶部内容行，在原 PaintCanvas 绘制三灯。当前核验 8DE675E4-DC25-455F-4C4C-44205044422E / C4935A66-4D7D-C7AC-4C4C-44205044422E，均为 age 1，其他编译版本需新增 ABI profile；不按 Edge 或业务控件名分派 |
| NetUI / Ribbon x64 | NetUIHWND + UIRibbon.dll；验证渲染库 CodeView ABI，在原顶部行插入 spacer，由原布局移动 QAT/标题；保留 DWM 非客户区、透明缩放边距和系统圆角，只隐藏真实 WS_SYSMENU；进程内只读样式兼容层让原框架继续看到原系统菜单位和 caption bounds；在原 HWND 绘制消息中画三圆点，没有新 HWND |
| Windows XAML Islands x64 | 按 Windows.UI.Xaml.dll CodeView 和函数指令验证框架 ABI，枚举已有 Island；公开 diagnostics 作后备，校验 source HWND、目标和 GUI 线程。按窗口操作语义、窗口标题和原行几何选择布局。混合标题保留原父子关系、Loaded/Unloaded 生命周期及列索引，仅在原行预留空间，隐藏 DWM 原按钮，使用框架主题资源；Padding 将三灯留在原 Grid 命中范围内；额外的原生拖动区域按 HTCAPTION 几何发现并处理三灯命中，键盘和可访问性沿用 XAML Button |
| ApplicationFrame / UWP x64 | 校验 ApplicationFrame.dll CodeView、函数入口与原 CTitleBar；在原 IDCompositionSurface 绘制、移动原按钮矩形，框架通知原内容标题腾位；公开 UWP XAML API 为原 NavigationView 返回按钮容器留白；同时处理原有可见/隐藏输入窗口，使用原框架按钮动作；没有新 HWND 或标题行 |

原生 Win32 保留原 NCCALCSIZE 和原外框、客户区尺寸，统一绘制原非客户区四边，按原可见边界裁切；正常窗口圆角、最大化矩形，撤销恢复原区域。缓冲绘制使用 BPPF_NOCLIP，防止区域裁切后遗漏边缘像素。原生菜单和滚动条沿用原区域、命中测试与输入；深色滚动条在宿主绘制回调后更新，避免最大化还原后出现浅色轨道。嵌入式 Chromium/D3D 子窗口在附加时执行一次 1 像素尺寸往返来更新旧合成坐标，确认前恢复精确尺寸；没有持续跟随或定位。控件树后端同样要求客户区和外框尺寸不变。

所有实际后端都优先隐藏或复用原操作按钮，在左上角嵌入红绿灯并让原布局腾出空间。没有外部跟随窗口、遮罩窗口、定位定时器或新增标题栏回退。750ms 定时器仅检查所有者和原树生命周期。

Electron overlay 没有公开 getter 读取最初的完整颜色设置；初值由当前主题、透明背景和实际行高推断，后续 setter 调用会保存。自定义初始 overlay 配色未完成精确还原验证。页面刷新、软件升级和跨屏 DPI 仍需单独验证。

NetUI 样式读取兼容层只处理当前已加载模块的已解析导入，不覆盖第三方已有 hook，未解析的 delay import 会跳过。仅对已附加 HWND 返回原 WS_SYSMENU 和原 caption bounds；同进程多个窗口共享引用计数，最后撤销恢复 IAT。新加载模块和跨屏 DPI 尚未验证。兼容层不应被理解为任意 NetUI 版本兼容。

NetUI 当前 ABI 为 UIRibbon.dll CodeView `FEE19CF7-23B1-A0AD-09AC-63E3F230B785` age 1。这是框架库约束，适用于满足同一原树结构的宿主；升级后不匹配则拒绝，不按 Explorer 程序名分派。

## 验证

v12 Edge 截图与逐 HWND 指标：`out/live-v12/edge-release/result.json`；真实鼠标、4 次 resize、重复应用和所有者退出恢复：`out/live-v12/bootstrap-input/result.json`。首次附加约 0.4 秒；x64 CTest 15/15、x86 7/7，正式包 Edge 原生输入复验通过。前台激活和实际光标到位均在点击前校验。早期失败记录、线程 Hook 126 的引导处理及未验证范围见 v12 检查记录。

v11 系统设置截图与逐 HWND 指标：`out/live-v11/settings-final/result.json`；真实鼠标：`out/live-v11/input-final/results.json`；11 分类页与宿主样式回放 23 帧：`out/live-v11/views-final/results.json`。

历史完整截图与逐 HWND 指标：`out/live-v10/release-screenshots/report.md`、`report.json`；按钮记录：`out/live-v10/input-final/results.json`。保存应用前、首次应用约 100ms、稳定后、最大化、还原大小和撤销后的画面，检查三色像素、客户区、原按钮、外框、无新增子 HWND、四边/四角、右侧/底部新空白和撤销残留。最大化/还原可引起宿主自身的尺寸调整，附加几何比较在动作前完成，撤销比较使用撤销前的实际几何。

使用原生 Win32、GDI 截图和必要的 UIA/指针输入，不使用 Computer Use。红灯关闭仅测试自有示例，不关闭用户应用。最终结果见 [本机兼容记录](compatibility-v12-2026-11-01.md)，失败及未适配不会计为通过。

```powershell
.\demos\window-styler\build.ps1 -QtRoot 'C:\Qt\6.11.1\msvc2022_64' -WpfSamples -Test
.\demos\window-styler\build.ps1 -Architecture Win32 -Test
.\demos\window-styler\package.ps1
# 管理员 PowerShell
.\demos\window-styler\test-controller-direct.ps1
.\demos\window-styler\test-live-input.ps1
.\demos\window-styler\screenshot-all.ps1 -CaptureUnsupported -HostRefresh
```

依赖 VS 2022 C++、CMake、.NET SDK/.NET 8 桌面目标包和系统 .NET Framework 编译器。Qt 后端/示例需要匹配的 Qt 开发目录。包内运行时只给自有示例使用，不替换目标进程已有运行时。

## 扩展

见 [C ABI](adapter-api.h) 和 [SDK](sdk/README.md)。`adapters.ini` 支持标准窗口类、子窗口类、属性、模块 AND/OR、导出能力及 ModuleCodeView ABI 身份约束；Framework 字段只作描述。通用内核与后端完全分离，添加一个框架无需修改注入器分派代码。

DLL 使用内容哈希命名并可能固定映射至目标进程退出。撤销移除布局修改和实例回调；保留已加载 DLL，避免覆盖仍被系统引用的代码。研究产物保存在 out 下，新的框架入口见 [v12 研究记录](research/frameworks-v12.md)，分发包使用 v12。

XAML ABI 当前验证 Windows.UI.Xaml.dll CodeView A5E5662D-B273-6140-B633-BC2D198B5A28 age 1（本机 10.0.26100.1742）。此入口属于框架内部接口，公开 PDB 符号不代表稳定公开 API。未知版本拒绝该入口，后备 diagnostics 无法取树时保持原样。混合标题不会替换 UserControl.Content 或重编号原 Grid 列。未完成框架的证据与后续入口见 [研究记录](research/frameworks-v9.md)。v8 证据和分发包单独保留。
